#include <QCoreApplication>
#include "desktop/store.h"
#include "desktop/record_actions.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QSet>
#include <QUuid>
#include <QUrl>
#include <functional>
#include <memory>

namespace crossdesk::desktop {
namespace {
bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
bool writeJson(const QString& file, const QJsonObject& value, QString* error) {
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly)) return fail(error, output.errorString());
    const auto bytes = QJsonDocument(value).toJson();
    if (output.write(bytes) != bytes.size() || !output.commit())
        return fail(error, output.errorString());
    return true;
}
bool readJson(const QString& file, QJsonObject& value, QString* error) {
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly)) return fail(error, input.errorString());
    if (input.size() > 32 * 1024 * 1024) return fail(error, QCoreApplication::translate("CrossDesk", "配置文件过大"));
    QJsonParseError parse;
    auto document = QJsonDocument::fromJson(input.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QCoreApplication::translate("CrossDesk", "配置文件格式无效"));
    value = document.object();
    return Store::validate(value, error);
}
QJsonObject emptyState() {
    return {{"schema", 1}, {"widgets", QJsonArray{}}, {"settings", QJsonObject{}},
            {"todos", QJsonArray{}}, {"notes", QJsonArray{}}, {"operations", QJsonArray{}}};
}
QString normalized(const QString& path) {
    const QFileInfo info(path);
    if (info.exists()) return QDir::cleanPath(info.canonicalFilePath());
    QString ancestor = info.absoluteFilePath();
    QStringList suffix;
    while (!QFileInfo::exists(ancestor)) {
        const QFileInfo current(ancestor);
        suffix.prepend(current.fileName());
        const auto parent = current.absolutePath();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    QString resolved = QFileInfo(ancestor).canonicalFilePath();
    if (resolved.isEmpty()) resolved = ancestor;
    for (const auto& component : suffix) resolved += '/' + component;
    return QDir::cleanPath(resolved);
}
bool inside(const QString& child, const QString& parent) {
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return child.compare(parent, sensitivity) == 0 ||
           child.startsWith(parent + '/', sensitivity);
}
}

Store::Store(QString directory, QObject* parent)
    : QObject(parent), directory_(std::move(directory)), state_(emptyState()) {}
bool Store::validate(const QJsonObject& state, QString* error) {
    if (state["schema"].toInt() != 1 || !state["widgets"].isArray() ||
        !state["settings"].isObject()) return fail(error, QCoreApplication::translate("CrossDesk", "不支持的配置版本"));
    QSet<QString> ids;
    const QSet<QString> kinds{"folder", "todo", "note", "clock", "weather", "music", "search"};
    for (const auto& value : state["widgets"].toArray()) {
        const auto item = value.toObject();
        const auto id = item["id"].toString();
        if (QUuid(id).isNull() || ids.contains(id) || !kinds.contains(item["kind"].toString()) ||
            !item["data"].isObject() || item["width"].toInt() < 180 ||
            item["height"].toInt() < 120 || item["width"].toInt() > 16000 ||
            item["height"].toInt() > 16000)
            return fail(error, QCoreApplication::translate("CrossDesk", "格子数据无效或标识重复"));
        ids.insert(id);
    }
    for (const auto* key : {"todos", "notes", "operations"})
        if (!state[key].isArray()) return fail(error, QCoreApplication::translate("CrossDesk", "记录数据无效"));
    for (const auto* key : {"todos", "notes"}) {
        QSet<QString> recordIds;
        for (const auto& value : state[key].toArray()) {
            const auto record = value.toObject();
            const auto id = record["id"].toString();
            if (QUuid(id).isNull() || recordIds.contains(id) || !record["title"].isString())
                return fail(error, QCoreApplication::translate("CrossDesk", "记录标识或标题无效"));
            recordIds.insert(id);
        }
    }
    return true;
}
bool Store::load(QString* error) {
    if (!QDir().mkpath(directory_)) return fail(error, QCoreApplication::translate("CrossDesk", "无法创建数据目录"));
    const auto file = directory_ + "/workspace.json";
    if (!QFileInfo::exists(file)) return true;
    QJsonObject loaded;
    QString primaryError;
    if (!readJson(file, loaded, &primaryError)) {
        if (!readJson(file + ".bak", loaded, nullptr)) return fail(error, primaryError);
        emit persistenceFailed(QCoreApplication::translate("CrossDesk", "主配置损坏，已读取上一次备份"));
    }
    state_ = loaded;
    emit changed();
    return true;
}
bool Store::save(QString* error) { return commit(state_, error); }
bool Store::commit(const QJsonObject& next, QString* error) {
    QString reason;
    if (!validate(next, &reason) || !QDir().mkpath(directory_)) {
        if (reason.isEmpty()) reason = QCoreApplication::translate("CrossDesk", "无法创建数据目录");
        emit persistenceFailed(reason);
        return fail(error, reason);
    }
    const auto file = directory_ + "/workspace.json";
    QJsonObject previous;
    if (QFileInfo::exists(file) && readJson(file, previous, nullptr) &&
        !writeJson(file + ".bak", previous, &reason)) {
        emit persistenceFailed(reason);
        return fail(error, reason);
    }
    if (!writeJson(file, next, &reason)) {
        emit persistenceFailed(reason);
        return fail(error, reason);
    }
    state_ = next;
    emit changed();
    return true;
}
QJsonObject Store::widget(const QString& id) const {
    for (const auto& item : widgets()) if (item.toObject()["id"].toString() == id) return item.toObject();
    return {};
}
QString Store::addWidget(const QString& kind, const QString& title, const QJsonObject& data) {
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto next = state_;
    auto list = widgets();
    list.append(QJsonObject{{"id", id}, {"kind", kind}, {"title", title}, {"data", data},
        {"x", 80 + list.size() * 24 % 400}, {"y", 100 + list.size() * 24 % 300},
        {"width", 360}, {"height", 320}, {"collapsed", false}, {"pinned", false}});
    next["widgets"] = list;
    return commit(next) ? id : QString{};
}
bool Store::putWidget(const QJsonObject& widget, QString* error) {
    auto list = widgets();
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].toObject()["id"] == widget["id"]) {
            list[i] = widget;
            auto next = state_; next["widgets"] = list;
            return commit(next, error);
        }
    }
    return fail(error, QCoreApplication::translate("CrossDesk", "格子已不存在"));
}
bool Store::removeWidget(const QString& id) {
    auto list = widgets();
    for (int i = 0; i < list.size(); ++i) if (list[i].toObject()["id"].toString() == id) {
        list.removeAt(i);
        auto next = state_; next["widgets"] = list;
        return commit(next);
    }
    return false;
}
bool Store::setRecords(const QString& key, const QJsonArray& records) {
    if (key != "todos" && key != "notes" && key != "operations") return false;
    auto next = state_; next[key] = records;
    return commit(next);
}
QString Store::convertRecord(const QString& source, const QString& destination, const QString& id) {
    if (!((source == "todos" && destination == "notes") || (source == "notes" && destination == "todos"))) return {};
    auto from = records(source); auto to = records(destination);
    for (int i = 0; i < from.size(); ++i) {
        const auto original = from[i].toObject(); if (original["id"].toString() != id) continue;
        const auto newId = QUuid::createUuid().toString();
        auto converted = original; converted["id"] = newId;
        if (destination == "notes") converted["body"] = original["notes"];
        else {
            converted["notes"] = original["body"]; converted["done"] = false;
            converted["due"] = QDateTime::currentDateTime().addDays(1).toString(Qt::ISODate);
            converted["repeat"] = 0; converted["reminder"] = false;
        }
        from.removeAt(i); to.append(converted);
        auto next = state_; next[source] = from; next[destination] = to;
        return commit(next) ? newId : QString{};
    }
    return {};
}
bool Store::setSettings(const QJsonObject& settings) {
    auto next = state_; next["settings"] = settings;
    return commit(next);
}
namespace {
QString pathIdentity(const QString& path) {
    auto result = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}
bool portableAssetPath(const QString& path) {
    if (!path.startsWith("attachments/") || path == "attachments/" ||
        QDir::cleanPath(path) != path || path.contains('\\')) return false;
    static const QRegularExpression bad(QStringLiteral("[\\x00-\\x1f<>:\"|?*]"));
    static const QRegularExpression reserved(QStringLiteral("^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"), QRegularExpression::CaseInsensitiveOption);
    for (const auto& part : path.split('/'))
        if (part.isEmpty() || part.endsWith('.') || part.endsWith(' ') || bad.match(part).hasMatch() || reserved.match(part).hasMatch()) return false;
    return true;
}
QStringList attachmentReferences(const QJsonObject& state) {
    QStringList paths;
    for (const auto* key : {"todos", "notes"}) for (const auto& value : state[key].toArray())
        paths << recordAttachmentPaths(value.toObject());
    paths.removeDuplicates(); paths.sort(); return paths;
}
QJsonValue rewritePaths(const QJsonValue& value, const QMap<QString, QString>& paths, bool urls = true) {
    if (value.isString()) {
        auto text = value.toString();
        const auto exact = paths.constFind(pathIdentity(text));
        if (exact != paths.cend()) return exact.value();
        // Only complete file URLs are replaced; an attachment named a.txt must
        // never rewrite a different link named a.txt2.
        static const QRegularExpression links(QStringLiteral("file:[^\\s)<>]+"));
        struct Edit { int position; int count; QString text; };
        QList<Edit> edits; auto matches = links.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next(); const auto url = QUrl(match.captured());
            if (!url.isLocalFile()) continue;
            const auto replacement = paths.constFind(pathIdentity(url.toLocalFile()));
            if (replacement != paths.cend()) edits.prepend({int(match.capturedStart()), int(match.capturedLength()),
                urls ? QUrl::fromLocalFile(replacement.value()).toString(QUrl::FullyEncoded) : replacement.value()});
        }
        for (const auto& edit : edits) text.replace(edit.position, edit.count, edit.text);
        return text;
    }
    if (value.isArray()) { QJsonArray result; for (const auto& item : value.toArray()) result.append(rewritePaths(item, paths, urls)); return result; }
    if (value.isObject()) {
        auto result = value.toObject(); for (auto it = result.begin(); it != result.end(); ++it) it.value() = rewritePaths(it.value(), paths, urls);
        return result;
    }
    return value;
}
QByteArray recordSignature(QJsonObject record, const QMap<QString, QString>& assets) {
    if (record["title"] == record["restoreCopyTitle"] && record["restoreSourceTitle"].isString()) record["title"] = record["restoreSourceTitle"];
    for (const auto* key : {"id", "restoreOriginId", "restoreSourceTitle", "restoreCopyTitle"}) record.remove(key);
    return QCryptographicHash::hash(QJsonDocument(rewritePaths(record, assets, false).toObject()).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
}
}
bool Store::exportBackup(const QString& file, QString* error) const {
    auto backup = state_; QJsonArray assets; qint64 total = 0;
    const auto base = QFileInfo(directory_ + "/attachments").absoluteFilePath();
    QSet<QString> included, required;
    for (const auto* key : {"todos", "notes"}) for (const auto& value : state_[key].toArray())
        for (const auto& path : value.toObject()["attachments"].toArray()) if (path.isString()) required.insert(pathIdentity(path.toString()));
    for (const auto& path : attachmentReferences(state_)) {
        const auto identity = pathIdentity(path); if (!inside(identity, pathIdentity(base))) continue;
        if (included.contains(identity)) continue; included.insert(identity);
        const QFileInfo info(path); const auto relative = QDir(directory_).relativeFilePath(info.absoluteFilePath());
        // A missing image/link in Markdown is retained as text. Explicitly
        // managed attachments must exist so a backup cannot silently omit them.
        if (!info.exists() && !required.contains(identity) && !info.isSymbolicLink() && !info.isJunction()) continue;
        if (!portableAssetPath(relative) || !info.isFile() || info.isSymbolicLink() || info.isJunction() ||
            !inside(pathIdentity(info.canonicalFilePath()), pathIdentity(QFileInfo(directory_).canonicalFilePath() + "/attachments")))
            return fail(error, QCoreApplication::translate("CrossDesk", "托管附件不存在或路径不安全，备份未导出"));
        if (total + info.size() > 16 * 1024 * 1024)
            return fail(error, QCoreApplication::translate("CrossDesk", "此便携备份的附件上限为 16 MB，请单独备份较大的附件目录"));
        QFile input(path); if (!input.open(QIODevice::ReadOnly)) return fail(error, input.errorString());
        const auto bytes = input.readAll(); total += bytes.size();
        if (input.error() != QFile::NoError || total > 16 * 1024 * 1024)
            return fail(error, QCoreApplication::translate("CrossDesk", "附件读取失败或超过备份上限"));
        assets.append(QJsonObject{{"path", relative},
            {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
            {"base64", QString::fromLatin1(bytes.toBase64())}});
    }
    backup["backupAssets"] = assets; backup["backupRoot"] = QFileInfo(directory_).absoluteFilePath();
    if (QJsonDocument(backup).toJson().size() > 32 * 1024 * 1024) return fail(error, QCoreApplication::translate("CrossDesk", "备份超过 32 MB 上限"));
    return writeJson(file, backup, error);
}
bool Store::importBackup(const QString& file, QString* error) { return importBackup(file, RestoreMode::ReplaceData, error); }
bool Store::importBackup(const QString& file, RestoreMode mode, QString* error) {
    QJsonObject loaded;
    if (!readJson(file, loaded, error)) return false;
    const auto assets = loaded["backupAssets"].toArray();
    QMap<QString, QByteArray> decoded; QMap<QString, QString> assetPaths, tokens; qint64 total = 0;
    const auto originalRoot = loaded["backupRoot"].toString();
    if (!assets.isEmpty() && !QDir::isAbsolutePath(originalRoot))
        return fail(error, QCoreApplication::translate("CrossDesk", "备份来源目录无效"));
    for (const auto& value : assets) {
        const auto asset = value.toObject(); const auto path = asset["path"].toString();
        const auto identity = pathIdentity(originalRoot + '/' + path);
        if (!portableAssetPath(path) || decoded.contains(identity))
            return fail(error, QCoreApplication::translate("CrossDesk", "备份附件路径无效"));
        const auto result = QByteArray::fromBase64Encoding(asset["base64"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (result.decodingStatus != QByteArray::Base64DecodingStatus::Ok ||
            QCryptographicHash::hash(result.decoded, QCryptographicHash::Sha256).toHex() != asset["sha256"].toString().toLatin1())
            return fail(error, QCoreApplication::translate("CrossDesk", "备份附件校验失败"));
        total += result.decoded.size(); if (total > 16 * 1024 * 1024) return fail(error, QCoreApplication::translate("CrossDesk", "备份附件过大"));
        decoded[identity] = result.decoded; assetPaths[identity] = path;
        tokens[identity] = "attachment-sha256:" + asset["sha256"].toString().toLower();
    }
    QJsonObject next = mode == RestoreMode::MergeRecords ? state_ : loaded;
    next.remove("backupAssets"); next.remove("backupRoot"); next["settings"] = settings();
    if (mode == RestoreMode::ReplaceData) next["operations"] = QJsonArray{};
    QMap<QString, QString> currentTokens;
    auto currentSignature = [&](const QJsonObject& record) {
        for (const auto& path : recordAttachmentPaths(record)) {
            const auto identity = pathIdentity(path);
            if (currentTokens.contains(identity) || !inside(identity, pathIdentity(directory_ + "/attachments"))) continue;
            QFile input(path); const QFileInfo info(path);
            if (!info.isFile() || info.size() > 16 * 1024 * 1024 || !input.open(QIODevice::ReadOnly)) continue;
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (hash.addData(&input)) currentTokens[identity] = "attachment-sha256:" + QString::fromLatin1(hash.result().toHex());
        }
        return recordSignature(record, currentTokens);
    };
    QJsonObject imported{{"todos", QJsonArray{}}, {"notes", QJsonArray{}}};
    for (const auto* key : {"todos", "notes"}) {
        auto records = mode == RestoreMode::MergeRecords ? next[key].toArray() : QJsonArray{};
        QJsonArray additions;
        for (const auto& value : loaded[key].toArray()) {
            auto record = value.toObject(); const auto id = record["id"].toString();
            if (mode == RestoreMode::MergeRecords) {
                QJsonObject collision;
                for (const auto& current : records) if (current.toObject()["id"].toString() == id) { collision = current.toObject(); break; }
                if (!collision.isEmpty()) {
                    const auto signature = recordSignature(record, tokens);
                    bool duplicate = currentSignature(collision) == signature;
                    for (const auto& current : records) {
                        const auto copy = current.toObject();
                        if (!duplicate && copy["restoreOriginId"].toString() == id && currentSignature(copy) == signature) duplicate = true;
                    }
                    if (duplicate) continue;
                    const auto sourceTitle = record["title"].toString();
                    const auto copyTitle = sourceTitle + QCoreApplication::translate("CrossDesk", "（导入冲突副本）");
                    record["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    record["restoreOriginId"] = id; record["restoreSourceTitle"] = sourceTitle;
                    record["title"] = copyTitle; record["restoreCopyTitle"] = copyTitle;
                }
            }
            records.append(record); additions.append(record);
        }
        next[key] = records; imported[key] = additions;
    }
    if (!Store::validate(next, error)) return false;
    // Decode and validate the complete archive first, but materialize only the
    // files referenced by imported records. Old orphan restore trees stay out.
    QSet<QString> needed;
    for (const auto& path : attachmentReferences(imported)) if (decoded.contains(pathIdentity(path))) needed.insert(pathIdentity(path));
    const auto attachmentBase = directory_ + "/attachments";
    if (!needed.isEmpty() && (!QDir().mkpath(attachmentBase) || QFileInfo(attachmentBase).isSymbolicLink() || QFileInfo(attachmentBase).isJunction() ||
        pathIdentity(QFileInfo(attachmentBase).canonicalFilePath()) != pathIdentity(QFileInfo(directory_).canonicalFilePath() + "/attachments")))
        return fail(error, QCoreApplication::translate("CrossDesk", "附件恢复目录不安全，原配置已保留"));
    std::unique_ptr<QTemporaryDir> staging;
    if (!needed.isEmpty()) {
        staging = std::make_unique<QTemporaryDir>(attachmentBase + "/.import-XXXXXX");
        if (!staging->isValid()) return fail(error, QCoreApplication::translate("CrossDesk", "无法恢复附件目录"));
    }
    const auto targetRoot = attachmentBase + "/restored-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QMap<QString, QString> paths;
    for (const auto& identity : needed) {
        const auto relative = assetPaths[identity].mid(QString("attachments/").size());
        const auto temporary = staging->path() + '/' + relative;
        if (!QDir().mkpath(QFileInfo(temporary).absolutePath())) return fail(error, QCoreApplication::translate("CrossDesk", "无法恢复附件目录"));
        QSaveFile output(temporary);
        if (!output.open(QIODevice::WriteOnly) || output.write(decoded[identity]) != decoded[identity].size() || !output.commit())
            return fail(error, QCoreApplication::translate("CrossDesk", "附件恢复失败，原配置已保留"));
        paths[identity] = targetRoot + '/' + relative;
    }
    // Rebase only imported records, never existing local records or settings.
    for (const auto* key : {"todos", "notes"}) {
        auto records = next[key].toArray(); const int first = records.size() - imported[key].toArray().size();
        for (int i = first; i < records.size(); ++i) records[i] = rewritePaths(records[i], paths);
        next[key] = records;
    }
    if (!needed.isEmpty() && !QDir().rename(staging->path(), targetRoot))
        return fail(error, QCoreApplication::translate("CrossDesk", "无法提交恢复附件，原配置已保留"));
    if (next == state_ || commit(next, error)) {
        if (!needed.isEmpty()) staging->setAutoRemove(false);
        return true;
    }
    // targetRoot is the unique directory created by this import, never an old
    // attachment tree. Remove it only when the state commit did not succeed.
    if (!needed.isEmpty()) QDir(targetRoot).removeRecursively();
    return false;
}

FileResult copyPath(const QString& source, const QString& destination) {
    QFileInfo input(source);
    if (!input.exists() || input.isSymLink())
        return {false, QCoreApplication::translate("CrossDesk", "来源不存在或是符号链接"), destination};
    if (inside(normalized(destination), normalized(source)))
        return {false, QCoreApplication::translate("CrossDesk", "不能复制到自身或自己的子目录"), destination};
    if (QFileInfo::exists(destination))
        return {false, QCoreApplication::translate("CrossDesk", "目标已存在，未覆盖"), destination};
    if (input.isDir()) {
        if (!QDir().mkpath(destination)) return {false, QCoreApplication::translate("CrossDesk", "无法创建目标目录"), destination};
        for (const auto& child : QDir(source).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
            auto result = copyPath(child.absoluteFilePath(), destination + '/' + child.fileName());
            if (!result.ok) return result; // Preserve both source and completed destination files.
        }
        return {true, {}, destination};
    }
    QFile read(source);
    QTemporaryFile write(destination + ".XXXXXX");
    if (!read.open(QIODevice::ReadOnly) || !write.open())
        return {false, QCoreApplication::translate("CrossDesk", "无法打开来源或目标文件"), destination};
    QCryptographicHash sourceHash(QCryptographicHash::Sha256);
    while (!read.atEnd()) {
        const auto bytes = read.read(1024 * 1024);
        if (bytes.isEmpty() && read.error() != QFile::NoError)
            return {false, read.errorString(), destination};
        sourceHash.addData(bytes);
        if (write.write(bytes) != bytes.size()) return {false, write.errorString(), destination};
    }
    if (!write.flush()) return {false, write.errorString(), destination};
    QCryptographicHash targetHash(QCryptographicHash::Sha256);
    if (!write.seek(0) || !targetHash.addData(&write))
        return {false, QCoreApplication::translate("CrossDesk", "无法校验临时副本"), destination};
    if (sourceHash.result() != targetHash.result())
        return {false, QCoreApplication::translate("CrossDesk", "复制校验失败，来源已保留"), destination};
    write.close();
    // Refuse replacement even if the target appeared during the copy.
    if (!write.rename(destination))
        return {false, QCoreApplication::translate("CrossDesk", "无法提交副本，目标可能已存在"), destination};
    write.setAutoRemove(false);
    return {true, {}, destination};
}
FileResult movePath(const QString& source, const QString& destination) {
    if (QFileInfo::exists(destination) || inside(normalized(destination), normalized(source)))
        return {false, QCoreApplication::translate("CrossDesk", "目标已存在或位于来源内部"), destination};
    const bool ok = QFileInfo(source).isDir() ? QDir().rename(source, destination)
                                            : QFile::rename(source, destination);
    return {ok, ok ? QString{} : QCoreApplication::translate("CrossDesk", "移动失败；来源未被删除"), destination};
}
QString categoryFor(const QString& path) {
    if (QFileInfo(path).isDir()) return QCoreApplication::translate("CrossDesk", "文件夹");
    const auto ext = QFileInfo(path).suffix().toLower();
    for (const auto& pair : {qMakePair(QCoreApplication::translate("CrossDesk", "图片"), QStringLiteral("png jpg jpeg webp gif bmp svg")),
                            qMakePair(QCoreApplication::translate("CrossDesk", "文档"), QStringLiteral("pdf doc docx txt md xls xlsx ppt pptx")),
                            qMakePair(QCoreApplication::translate("CrossDesk", "影音"), QStringLiteral("mp4 mkv avi mp3 wav flac m4a")),
                            qMakePair(QCoreApplication::translate("CrossDesk", "压缩包"), QStringLiteral("zip 7z rar tar gz")),
                            qMakePair(QCoreApplication::translate("CrossDesk", "快捷方式"), QStringLiteral("lnk url"))})
        if (pair.second.split(' ').contains(ext)) return pair.first;
    return QCoreApplication::translate("CrossDesk", "其他");
}
bool recordMove(Store& store, const QString& source, const QString& destination) {
    auto operations = store.records("operations");
    operations.append(QJsonObject{{"source", source}, {"destination", destination},
        {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"undone", false}});
    while (operations.size() > 1000) operations.removeFirst();
    return store.setRecords("operations", operations);
}
QList<Reminder> dueReminders(const QJsonArray& tasks, const QDateTime& now,
                            const QSet<QString>& acknowledged) {
    QList<Reminder> result;
    if (!now.isValid()) return result;
    for (const auto& value : tasks) {
        const auto task = value.toObject(); const auto id = task["id"].toString();
        const auto dueText = task["due"].toString();
        const auto due = QDateTime::fromString(dueText, Qt::ISODate);
        const auto key = id + '|' + dueText;
        if (id.isEmpty() || task["done"].toBool() || !task["reminder"].toBool() ||
            !due.isValid() || due > now || acknowledged.contains(key)) continue;
        result.append({key, task["title"].toString()});
    }
    return result;
}
} // namespace crossdesk::desktop
