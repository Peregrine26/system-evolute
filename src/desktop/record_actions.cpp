#include <QCoreApplication>
#include "desktop/record_actions.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

namespace crossdesk::desktop {
namespace {
bool fail(QString* error, const QString& value) { if (error) *error = value; return false; }
bool validKind(const QString& kind) { return kind == "todos" || kind == "notes"; }
QString canonical(const QString& path) { return QDir::cleanPath(QFileInfo(path).canonicalFilePath()); }
bool isManaged(const Store& store, const QString& path) {
    const QFileInfo info(path); if (!info.exists() || info.isSymLink() || !info.isFile()) return false;
    const auto root = canonical(store.directory() + "/attachments");
    const auto candidate = canonical(path);
    if (root.isEmpty() || candidate.isEmpty()) return false;
#ifdef Q_OS_WIN
    return candidate.startsWith(root + '/', Qt::CaseInsensitive);
#else
    return candidate.startsWith(root + '/');
#endif
}
QString removeEmbeddedPath(const QString& markdown, const QString& path) {
    // Strip only complete local links to this exact file; surrounding prose remains.
    const QRegularExpression link(R"(!?\[[^\]\n]*(?:\\\][^\]\n]*)*\]\((file:[^\s\)]+)\))");
    QString result = markdown; auto matches = link.globalMatch(markdown);
    QList<QPair<int, int>> removals;
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (QFileInfo(QUrl(match.captured(1)).toLocalFile()).absoluteFilePath() == QFileInfo(path).absoluteFilePath())
            removals.prepend({int(match.capturedStart()), int(match.capturedLength())});
    }
    for (const auto& removal : removals) result.remove(removal.first, removal.second);
    return result;
}
}
QJsonObject findRecord(const Store& store, const QString& kind, const QString& id) {
    if (!validKind(kind)) return {};
    for (const auto& value : store.records(kind)) if (value.toObject()["id"].toString() == id) return value.toObject();
    return {};
}
bool patchRecord(Store& store, const QString& kind, const QString& id, const QJsonObject& fields) {
    if (!validKind(kind) || fields.contains("id")) return false;
    auto records = store.records(kind);
    for (int i = 0; i < records.size(); ++i) {
        auto record = records[i].toObject(); if (record["id"].toString() != id) continue;
        for (auto it = fields.begin(); it != fields.end(); ++it) record[it.key()] = it.value();
        records[i] = record; return store.setRecords(kind, records);
    }
    return false;
}
bool removeRecords(Store& store, const QString& kind, const QStringList& ids) {
    if (!validKind(kind) || ids.isEmpty()) return false;
    auto records = store.records(kind); bool changed = false;
    for (int i = records.size() - 1; i >= 0; --i)
        if (ids.contains(records[i].toObject()["id"].toString())) { records.removeAt(i); changed = true; }
    return changed && store.setRecords(kind, records);
}
bool completeTasks(Store& store, const QStringList& ids, bool done, const QDateTime& now) {
    if (ids.isEmpty()) return false;
    auto records = store.records("todos"); QJsonArray occurrences; bool changed = false;
    for (int i = 0; i < records.size(); ++i) {
        auto task = records[i].toObject(); if (!ids.contains(task["id"].toString())) continue;
        const bool wasDone = task["done"].toBool(); task["done"] = done;
        if (done && !wasDone && !task["nextCreated"].toBool() &&
            (task["repeat"].toInt() == 1 || task["repeat"].toInt() == 2)) {
            auto due = QDateTime::fromString(task["due"].toString(), Qt::ISODate);
            if (due.isValid() && now.isValid()) {
                const int step = task["repeat"].toInt() == 1 ? 1 : 7;
                // Skip expired occurrences arithmetically, preserving local wall time across DST.
                const auto elapsed = qMax<qint64>(0, due.date().daysTo(now.date()));
                const auto steps = qMax<qint64>(1, elapsed / step);
                due = due.addDays(steps * step);
                while (due.isValid() && due < now) due = due.addDays(step);
                if (due.isValid()) {
                    task["nextCreated"] = true;
                    auto next = task; next["id"] = QUuid::createUuid().toString();
                    next["done"] = false; next["nextCreated"] = false; next["due"] = due.toString(Qt::ISODate);
                    occurrences.append(next);
                }
            }
        }
        changed = changed || records[i].toObject() != task; records[i] = task;
    }
    for (const auto& value : occurrences) records.append(value);
    return changed && store.setRecords("todos", records);
}
QStringList recordAttachmentPaths(const QJsonObject& record) {
    QStringList result;
    for (const auto& value : record["attachments"].toArray()) if (value.isString() && !value.toString().isEmpty()) result << value.toString();
    const QRegularExpression links(R"(\]\((file:[^\s\)]+)\))");
    auto matches = links.globalMatch(record["body"].toString() + '\n' + record["notes"].toString());
    while (matches.hasNext()) {
        const auto path = QUrl(matches.next().captured(1)).toLocalFile(); if (!path.isEmpty()) result << path;
    }
    result.removeDuplicates(); return result;
}
bool addRecordAttachments(Store& store, const QString& kind, const QString& id,
                          const QStringList& paths, bool managed, QString* error) {
    if (findRecord(store, kind, id).isEmpty()) return fail(error, QCoreApplication::translate("CrossDesk", "记录已删除，附件未添加"));
    QStringList added; QStringList failures;
    for (const auto& path : paths) {
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile() || info.isSymLink()) { failures << info.fileName() + QCoreApplication::translate("CrossDesk", "：不是可读取的普通文件"); continue; }
        if (!managed) { added << info.absoluteFilePath(); continue; }
        const auto directory = store.directory() + "/attachments/" + id;
        if (!QDir().mkpath(directory)) { failures << QCoreApplication::translate("CrossDesk", "无法创建附件目录"); continue; }
        const auto target = directory + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + '-' + info.fileName();
        const auto copied = copyPath(info.absoluteFilePath(), target);
        if (copied.ok) added << copied.destination; else failures << info.fileName() + ": " + copied.error;
    }
    auto record = findRecord(store, kind, id);
    if (record.isEmpty()) return fail(error, QCoreApplication::translate("CrossDesk", "记录已删除，复制的附件已保留在数据目录"));
    auto attachments = record["attachments"].toArray();
    for (const auto& path : added) if (!attachments.contains(path)) attachments.append(path);
    if (!added.isEmpty() && !patchRecord(store, kind, id, {{"attachments", attachments}})) return fail(error, QCoreApplication::translate("CrossDesk", "附件保存失败，复制文件已保留"));
    if (!failures.isEmpty()) return fail(error, failures.join('\n'));
    return !added.isEmpty();
}
bool removeRecordAttachments(Store& store, const QString& kind, const QString& id,
                             const QStringList& paths, bool deleteManaged, QString* error) {
    auto record = findRecord(store, kind, id); if (record.isEmpty()) return fail(error, QCoreApplication::translate("CrossDesk", "记录已删除"));
    QJsonArray kept;
    for (const auto& value : record["attachments"].toArray()) if (!paths.contains(value.toString())) kept.append(value);
    QJsonObject patch{{"attachments", kept}};
    for (const auto* field : {"body", "notes"}) {
        auto text = record[field].toString(); for (const auto& path : paths) text = removeEmbeddedPath(text, path);
        if (record.contains(field)) patch[field] = text;
    }
    if (!patchRecord(store, kind, id, patch)) return fail(error, QCoreApplication::translate("CrossDesk", "无法保存附件变更"));
    QStringList failures;
    if (deleteManaged) for (const auto& path : paths) {
        if (!isManaged(store, path)) continue; // External originals are never removed.
        bool referenced = false;
        const auto identity = canonical(path);
        for (const auto* key : {"todos", "notes"}) for (const auto& value : store.records(key))
            for (const auto& other : recordAttachmentPaths(value.toObject()))
                if (canonical(other).compare(identity,
#ifdef Q_OS_WIN
                    Qt::CaseInsensitive
#else
                    Qt::CaseSensitive
#endif
                ) == 0) referenced = true;
        if (referenced) continue;
        if (!QFile::remove(path)) failures << QFileInfo(path).fileName() + QCoreApplication::translate("CrossDesk", "：托管副本未删除");
    }
    return failures.isEmpty() || fail(error, failures.join('\n'));
}
}
