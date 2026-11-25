#include "desktop/page_factories.h"
#include "desktop/file_transfer.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemModel>
#include <QListView>
#include <QPushButton>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <memory>
using namespace crossdesk::desktop;
bool expect(bool value, const char* message) { if (!value) std::cerr << message << '\n'; return value; }
int main(int argc, char** argv) {
    QApplication app(argc, argv); QTemporaryDir root; bool ok = root.isValid();
    const auto source = root.path() + "/source.bin", copied = root.path() + "/copied.bin";
    { QFile file(source); ok &= file.open(QIODevice::WriteOnly); file.write(QByteArray(4 * 1024 * 1024, 'x')); }
    std::atomic_bool cancel(false); int chunks = 0;
    auto result = copyPathControlled(source, copied, cancel, [&](qint64, qint64, const QString&) { ++chunks; cancel = true; });
    ok &= expect(!result.ok && chunks == 1 && QFile::exists(source) && !QFile::exists(copied), "cancelled copy committed a target or deleted source");
    cancel = false;
    ok &= expect(copyPathControlled(source, copied, cancel).ok && QFileInfo(copied).size() == QFileInfo(source).size(), "verified copy failed");
    ok &= expect(!copyPathControlled(source, copied, cancel).ok, "copy overwrote target");
    const auto folder = root.path() + "/mapped"; QDir().mkpath(folder + "/child");
    for (const auto* name : {"alpha.txt", "beta.txt", "image.png"}) { QFile file(folder + '/' + name); file.open(QIODevice::WriteOnly); file.write(name); }
    ok &= expect(copyPathControlled(folder, root.path() + "/folder-copy", cancel).ok && QFile::exists(root.path() + "/folder-copy/beta.txt"), "staged directory copy failed");
    ok &= expect(!copyPathControlled(folder, folder + "/nested", cancel).ok, "self-nested copy accepted");
#ifdef Q_OS_WIN
    QString error;
    ok &= expect(createFileShortcut(source, root.path() + "/source.lnk", &error) && QFileInfo(root.path() + "/source.lnk").size() > 0, "native shortcut creation failed");
    ok &= expect(!createFileShortcut(source, root.path() + "/source.lnk", &error), "shortcut overwrote existing file");
    ok &= expect(copyPathControlled(root.path() + "/source.lnk", root.path() + "/copied.lnk", cancel).ok, "ordinary Windows shortcut copy rejected as symbolic link");
    { QFile original(root.path() + "/source.lnk"), copy(root.path() + "/copied.lnk"); original.open(QIODevice::ReadOnly); copy.open(QIODevice::ReadOnly);
      ok &= expect(original.readAll() == copy.readAll(), "shortcut copy followed target instead of preserving link bytes"); }
#endif
    Store store(root.path() + "/profile"); ok &= store.load();
    const auto id = store.addWidget("folder", "files", {{"path", folder}, {"sort", 4}, {"iconSize", 72},
        {"orders", QJsonObject{{folder, QJsonArray{"beta.txt", "alpha.txt", "image.png", "child"}}}}});
    std::unique_ptr<QWidget> page(createFolderPage(store, id, [](const QString&) {}));
    auto* view = page->findChild<QListView*>("folderView");
    auto* model = page->findChild<QFileSystemModel*>("folderFileModel");
    auto* proxy = page->findChild<QSortFilterProxyModel*>("folderSortModel");
    ok &= expect(view && model && proxy, "file page models missing"); if (!ok) return 1;
    QElapsedTimer timer; timer.start();
    while (proxy->rowCount(view->rootIndex()) < 4 && timer.elapsed() < 4000) app.processEvents();
    ok &= expect(proxy->index(0, 0, view->rootIndex()).data().toString() == "beta.txt", "persisted manual ordering not applied");
    ok &= expect(view->iconSize() == QSize(72, 72), "per-widget icon size not restored");
    page.reset();
    Store reloaded(root.path() + "/profile"); ok &= reloaded.load();
    ok &= expect(reloaded.widget(id)["data"].toObject()["sort"].toInt() == 4 && reloaded.widget(id)["data"].toObject()["orders"].toObject()[folder].toArray().first() == "beta.txt", "manual order did not survive reload");
    const auto target = root.path() + "/batch"; QDir().mkpath(target);
    const auto summary = transferFiles(store, {folder + "/alpha.txt", folder + "/beta.txt"}, target, false, nullptr);
    ok &= expect(summary.completed == 2 && summary.errors.isEmpty() && !summary.cancelled && QFile::exists(target + "/alpha.txt"), "background transfer dialog failed");
    return ok ? 0 : 1;
}
