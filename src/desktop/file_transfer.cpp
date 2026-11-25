#include <QCoreApplication>
#include "desktop/file_transfer.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>
#include <QtConcurrent>
#include <QMutex>
#include <QMutexLocker>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#endif

namespace crossdesk::desktop {
namespace {
QString resolved(const QString& path) {
    QString candidate = QFileInfo(path).absoluteFilePath();
    QStringList rest;
    while (!QFileInfo::exists(candidate)) {
        const QFileInfo info(candidate);
        if (info.absolutePath() == candidate) break;
        rest.prepend(info.fileName()); candidate = info.absolutePath();
    }
    auto base = QFileInfo(candidate).canonicalFilePath();
    if (base.isEmpty()) base = candidate;
    for (const auto& part : rest) base += '/' + part;
    return QDir::cleanPath(base);
}
bool inside(const QString& child, const QString& parent) {
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return child.compare(parent, sensitivity) == 0 || child.startsWith(parent + '/', sensitivity);
}
FileResult copyFile(const QString& source, const QString& destination, const std::atomic_bool& cancel,
                    qint64& completed, qint64 total, const TransferProgress& progress) {
    const QFileInfo before(source);
    if (before.isSymbolicLink() || before.isJunction() || (!before.isFile() && !before.isShortcut()) || QFileInfo::exists(destination))
        return {false, QCoreApplication::translate("CrossDesk", "来源无效、符号链接或目标已存在"), destination};
    QFile input(source); QTemporaryFile output(destination + ".crossdesk-XXXXXX");
    if (!input.open(QIODevice::ReadOnly) || !output.open())
        return {false, QCoreApplication::translate("CrossDesk", "无法读取来源或创建副本"), destination};
    QCryptographicHash sourceHash(QCryptographicHash::Sha256);
    while (!input.atEnd()) {
        if (cancel) return {false, QCoreApplication::translate("CrossDesk", "已取消，来源已保留"), destination};
        const auto bytes = input.read(1024 * 1024);
        if (bytes.isEmpty() && input.error() != QFile::NoError) return {false, input.errorString(), destination};
        if (output.write(bytes) != bytes.size()) return {false, output.errorString(), destination};
        sourceHash.addData(bytes); completed += bytes.size();
        if (progress) progress(completed, total, source);
    }
    if (!output.flush() || !output.seek(0)) return {false, output.errorString(), destination};
    QCryptographicHash copiedHash(QCryptographicHash::Sha256);
    while (!output.atEnd()) {
        if (cancel) return {false, QCoreApplication::translate("CrossDesk", "已取消，来源已保留"), destination};
        auto bytes = output.read(1024 * 1024);
        if (bytes.isEmpty() && output.error() != QFile::NoError) return {false, output.errorString(), destination};
        copiedHash.addData(bytes);
    }
    const QFileInfo after(source);
    if (before.size() != after.size() || before.lastModified() != after.lastModified() ||
        sourceHash.result() != copiedHash.result()) return {false, QCoreApplication::translate("CrossDesk", "来源已变化或校验失败，未提交副本"), destination};
    if (cancel) return {false, QCoreApplication::translate("CrossDesk", "已取消，来源已保留"), destination};
    output.close();
    if (!output.rename(destination)) return {false, QCoreApplication::translate("CrossDesk", "无法提交副本，目标可能已存在"), destination};
    output.setAutoRemove(false);
    return {true, {}, destination};
}
FileResult nativeMove(const QString& source, const QString& destination) {
    if (!QFileInfo::exists(source) || QFileInfo(source).isSymbolicLink() || QFileInfo(source).isJunction() || QFileInfo::exists(destination) || inside(resolved(destination), resolved(source)))
        return {false, QCoreApplication::translate("CrossDesk", "来源无效、目标已存在或位于来源内部"), destination};
#ifdef Q_OS_WIN
    const auto from = QDir::toNativeSeparators(source).toStdWString();
    const auto to = QDir::toNativeSeparators(destination).toStdWString();
    // No COPY_ALLOWED or REPLACE_EXISTING: native rename is atomic and never
    // deletes a source following an interrupted cross-volume transfer.
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) return {true, {}, destination};
    return {false, QCoreApplication::translate("CrossDesk", "移动失败（%1）；来源已保留。跨盘请先复制并核对。").arg(GetLastError()), destination};
#else
    return movePath(source, destination);
#endif
}
}

FileResult copyPathControlled(const QString& source, const QString& destination,
                             const std::atomic_bool& cancelled, const TransferProgress& progress) {
    if (cancelled) return {false, QCoreApplication::translate("CrossDesk", "已取消"), destination};
    if (!QFileInfo::exists(source) || QFileInfo::exists(destination) ||
        inside(resolved(destination), resolved(source)) || QFileInfo(source).isSymbolicLink() || QFileInfo(source).isJunction())
        return {false, QCoreApplication::translate("CrossDesk", "来源无效、目标已存在或位于来源内部"), destination};
    qint64 total = 0, completed = 0;
    QStringList files, folders;
    if (QFileInfo(source).isDir() && !QFileInfo(source).isShortcut()) {
        QDirIterator it(source, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (cancelled) return {false, QCoreApplication::translate("CrossDesk", "已取消"), destination};
            const auto path = it.next(); const auto info = it.fileInfo();
            if (info.isSymbolicLink() || info.isJunction()) return {false, QCoreApplication::translate("CrossDesk", "目录包含符号链接，未复制"), destination};
            if (info.isDir() && !info.isShortcut()) folders << QDir(source).relativeFilePath(path);
            else if (info.isFile() || info.isShortcut()) { files << QDir(source).relativeFilePath(path); total += info.size(); }
            else return {false, QCoreApplication::translate("CrossDesk", "目录包含不支持的项目"), destination};
        }
        QTemporaryDir staging(QFileInfo(destination).absolutePath() + "/.crossdesk-copy-XXXXXX");
        if (!staging.isValid()) return {false, QCoreApplication::translate("CrossDesk", "无法创建临时目录"), destination};
        for (const auto& folder : folders) if (!QDir().mkpath(staging.path() + '/' + folder))
            return {false, QCoreApplication::translate("CrossDesk", "无法创建副本子目录"), destination};
        for (const auto& file : files) {
            const auto result = copyFile(source + '/' + file, staging.path() + '/' + file, cancelled, completed, total, progress);
            if (!result.ok) return {false, result.error, destination};
        }
        // Detect additions/removals before committing a directory snapshot.
        QStringList now;
        QDirIterator verify(source, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (verify.hasNext()) { if (cancelled) return {false, QCoreApplication::translate("CrossDesk", "已取消"), destination}; now << QDir(source).relativeFilePath(verify.next()); if (verify.fileInfo().isSymbolicLink() || verify.fileInfo().isJunction()) return {false, QCoreApplication::translate("CrossDesk", "来源已变化"), destination}; }
        auto expected = files + folders; expected.sort(); now.sort();
        if (now != expected || cancelled) return {false, QCoreApplication::translate("CrossDesk", "来源目录已变化或操作已取消"), destination};
        if (!QDir().rename(staging.path(), destination)) return {false, QCoreApplication::translate("CrossDesk", "无法提交目录，目标可能已存在"), destination};
        staging.setAutoRemove(false);
        return {true, {}, destination};
    }
    total = QFileInfo(source).size();
    return copyFile(source, destination, cancelled, completed, total, progress);
}

bool createFileShortcut(const QString& source, const QString& destination, QString* error) {
    if (!QFileInfo::exists(source) || QFileInfo::exists(destination)) {
        if (error) *error = QCoreApplication::translate("CrossDesk", "来源不存在或快捷方式已存在"); return false;
    }
#ifdef Q_OS_WIN
    QTemporaryFile pending(destination + ".shortcut-XXXXXX");
    if (!pending.open()) { if (error) *error = pending.errorString(); return false; }
    const auto pendingPath = pending.fileName(); pending.close();
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* link = nullptr; IPersistFile* file = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link));
    const auto path = QDir::toNativeSeparators(source).toStdWString();
    const auto target = QDir::toNativeSeparators(pendingPath).toStdWString();
    if (SUCCEEDED(result)) result = link->SetPath(path.c_str());
    if (SUCCEEDED(result)) result = link->SetWorkingDirectory(QDir::toNativeSeparators(QFileInfo(source).absolutePath()).toStdWString().c_str());
    if (SUCCEEDED(result)) result = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (SUCCEEDED(result)) result = file->Save(target.c_str(), TRUE);
    if (file) file->Release(); if (link) link->Release();
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (SUCCEEDED(result) && !pending.rename(destination)) result = HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    if (SUCCEEDED(result)) pending.setAutoRemove(false);
    if (FAILED(result) && error) *error = QCoreApplication::translate("CrossDesk", "创建快捷方式失败（%1）").arg(quint32(result));
    return SUCCEEDED(result);
#else
    const bool ok = QFile::link(source, destination);
    if (!ok && error) *error = QCoreApplication::translate("CrossDesk", "无法创建链接"); return ok;
#endif
}

TransferSummary transferFiles(Store& store, const QStringList& sources, const QString& directory, bool move, QWidget* parent) {
    TransferSummary summary;
    if (sources.isEmpty() || !QFileInfo(directory).isDir()) return summary;
    QStringList pending = sources;
    do {
        auto cancelled = std::make_shared<std::atomic_bool>(false);
        struct Progress { QMutex mutex; qint64 done = 0, total = 0; QString path; };
        auto progress = std::make_shared<Progress>();
        QProgressDialog dialog(move ? QCoreApplication::translate("CrossDesk", "移动文件") : QCoreApplication::translate("CrossDesk", "复制并校验文件"), QCoreApplication::translate("CrossDesk", "取消"), 0, 1000, parent);
        dialog.setWindowModality(Qt::WindowModal); dialog.setMinimumDuration(0); dialog.setAutoClose(false); dialog.setAutoReset(false);
        QObject::connect(&dialog, &QProgressDialog::canceled, &dialog, [cancelled] { *cancelled = true; });
        QFutureWatcher<QList<QPair<QString, FileResult>>> watcher;
        QObject::connect(&watcher, &QFutureWatcherBase::finished, &dialog, &QDialog::accept);
        QTimer timer; timer.setInterval(80);
        QObject::connect(&timer, &QTimer::timeout, &dialog, [&dialog, progress] {
            QMutexLocker lock(&progress->mutex); dialog.setLabelText(QFileInfo(progress->path).fileName());
            dialog.setValue(progress->total ? int(qMin<qint64>(1000, progress->done * 1000 / progress->total)) : 0);
        }); timer.start();
        watcher.setFuture(QtConcurrent::run([pending, directory, move, cancelled, progress] {
            QList<QPair<QString, FileResult>> results;
            for (const auto& source : pending) {
                if (*cancelled) break;
                const auto destination = directory + '/' + QFileInfo(source).fileName();
                auto report = [progress](qint64 done, qint64 total, const QString& path) {
                    QMutexLocker lock(&progress->mutex); progress->done = done; progress->total = total; progress->path = path;
                };
                results << qMakePair(source, move ? nativeMove(source, destination) : copyPathControlled(source, destination, *cancelled, report));
            }
            return results;
        }));
        dialog.exec();
        // A cancel keeps the dialog alive until the worker acknowledges it.
        if (!watcher.isFinished()) { *cancelled = true; watcher.waitForFinished(); }
        timer.stop(); QStringList failed; summary.errors.clear();
        for (const auto& result : watcher.result()) {
            if (result.second.ok) { ++summary.completed; if (move) recordMove(store, result.first, result.second.destination); }
            else { failed << result.first; summary.errors << QFileInfo(result.first).fileName() + ": " + result.second.error; }
        }
        summary.cancelled = *cancelled;
        if (summary.cancelled || failed.isEmpty()) break;
        if (QMessageBox::warning(parent, QCoreApplication::translate("CrossDesk", "部分文件未传输"), summary.errors.join('\n'), QMessageBox::Retry | QMessageBox::Close) != QMessageBox::Retry) break;
        pending = failed;
    } while (true);
    return summary;
}
}
