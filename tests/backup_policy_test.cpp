#include "desktop/store.h"
#include "desktop/record_actions.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <iostream>

using namespace crossdesk::desktop;
namespace {
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) { QFile file(path); file.open(QIODevice::ReadOnly); return file.readAll(); }
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); QTemporaryDir root; bool ok = root.isValid();
    const auto check = [&](bool value, const char* message) { if (!value) { ok = false; std::cerr << message << '\n'; } };
    Store source(root.path() + "/source"), target(root.path() + "/target"); check(source.load() && target.load(), "fixture stores failed");
    const auto sourceWidget = source.addWidget("note", "imported widget");
    source.setSettings({{"autoStart", true}, {"shortcut_overview", "Ctrl+Shift+1"}, {"language", "en"}, {"displayPreference", "source-monitor"}});
    source.setRecords("operations", {QJsonObject{{"source", "C:/old/from"}, {"destination", "C:/old/to"}, {"undone", false}}});
    const auto taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto noteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto asset = source.directory() + "/attachments/fixture/a.txt";
    const auto similar = source.directory() + "/attachments/fixture/a.txt2";
    const auto external = root.path() + "/external.txt";
    QDir().mkpath(QFileInfo(asset).absolutePath()); write(asset, "managed bytes"); write(external, "external bytes");
    // Orphan files, including large ones, must not inflate future backups.
    write(source.directory() + "/attachments/orphan.bin", QByteArray(17 * 1024 * 1024, 'x'));
    source.setRecords("todos", {QJsonObject{{"id", taskId}, {"title", "task"}, {"notes", "task body"}, {"done", false},
        {"attachments", QJsonArray{asset, external}}}});
    source.setRecords("notes", {QJsonObject{{"id", noteId}, {"title", "note"}, {"body", "[real](" + QUrl::fromLocalFile(asset).toString() + ")\n[prefix](" + QUrl::fromLocalFile(similar).toString() + ")"}}});
    const auto archive = root.path() + "/backup.json"; QString error;
    check(source.exportBackup(archive, &error), "referenced-only backup failed due to orphan size");
    if (!ok) { std::cerr << error.toStdString() << '\n'; return 1; }
    const auto document = QJsonDocument::fromJson(read(archive)).object();
    check(document["backupAssets"].toArray().size() == 1, "backup included orphan or external attachment");
    const QJsonObject localSettings{{"autoStart", false}, {"shortcut_overview", "Ctrl+Alt+M"}, {"language", "zh_CN"}, {"displayPreference", "local-monitor"}};
    target.setSettings(localSettings); const auto oldWidget = target.addWidget("clock", "local widget");
    target.setRecords("operations", {QJsonObject{{"source", "local-from"}, {"destination", "local-to"}}});
    check(target.importBackup(archive, &error), "data replace failed");
    check(target.settings() == localSettings && target.widget(sourceWidget)["title"] == "imported widget" && target.widget(oldWidget).isEmpty(), "replace overwrote local settings or failed to replace data");
    check(target.records("operations").isEmpty(), "replace imported stale external-file undo ledger");
    const auto restoredTask = target.records("todos").first().toObject();
    const auto restoredPath = restoredTask["attachments"].toArray().first().toString();
    check(restoredPath != asset && read(restoredPath) == "managed bytes" && restoredTask["attachments"].toArray()[1] == external, "managed/external attachment rebase incorrect");
    const auto restoredBody = target.records("notes").first().toObject()["body"].toString();
    check(restoredBody.contains(QUrl::fromLocalFile(restoredPath).toString(QUrl::FullyEncoded)) && restoredBody.contains(QUrl::fromLocalFile(similar).toString()), "file URL prefix replacement damaged unrelated link");

    const auto widgetsBeforeMerge = target.widgets();
    target.setRecords("operations", {QJsonObject{{"source", "local-a"}, {"destination", "local-b"}}}); const auto history = target.records("operations");
    check(target.importBackup(archive, RestoreMode::MergeRecords, &error) && target.records("todos").size() == 1 && target.records("notes").size() == 1,
        "same attachment content with rebased paths duplicated records");
    check(target.widgets() == widgetsBeforeMerge && target.settings() == localSettings && target.records("operations") == history, "merge changed widgets, machine settings or local undo history");
    patchRecord(target, "todos", taskId, {{"notes", "edited locally"}});
    check(target.importBackup(archive, RestoreMode::MergeRecords, &error) && target.records("todos").size() == 2, "UUID conflict did not preserve imported copy");
    check(findRecord(target, "todos", taskId)["notes"] == "edited locally", "merge overwrote local UUID collision");
    auto conflict = target.records("todos").last().toObject(); const auto conflictId = conflict["id"].toString();
    check(conflict["restoreSourceTitle"] == "task" && conflict["restoreCopyTitle"] == conflict["title"], "conflict metadata lost values during JSON insertion");
    check(conflictId != taskId && conflict["title"].toString().contains(QStringLiteral("冲突")) && conflict["notes"] == "task body", "conflict copy missing identity, label or content");
    check(read(conflict["attachments"].toArray().first().toString()) == "managed bytes", "conflict attachment not restored");
    check(target.importBackup(archive, RestoreMode::MergeRecords, &error) && target.records("todos").size() == 2, "repeating a merge duplicated the preserved conflict copy");
    patchRecord(target, "todos", conflictId, {{"notes", "copy edited locally"}});
    check(target.importBackup(archive, RestoreMode::MergeRecords, &error) && target.records("todos").size() == 3, "modified conflict copy prevented recovery of original backup content");

    const auto original = read(target.directory() + "/workspace.json"); const auto settingsBeforeFailure = target.settings();
    const auto tampered = root.path() + "/tampered.json";
    auto bad = document; auto badAssets = bad["backupAssets"].toArray(); auto entry = badAssets[0].toObject(); entry["path"] = "attachments/../../escape.txt"; badAssets[0] = entry; bad["backupAssets"] = badAssets; write(tampered, QJsonDocument(bad).toJson());
    check(!target.importBackup(tampered, RestoreMode::MergeRecords, &error) && read(target.directory() + "/workspace.json") == original && target.settings() == settingsBeforeFailure, "unsafe asset path modified live state");
    bad = document; badAssets = bad["backupAssets"].toArray(); entry = badAssets[0].toObject(); entry["sha256"] = QString(64, '0'); badAssets[0] = entry; bad["backupAssets"] = badAssets; write(tampered, QJsonDocument(bad).toJson());
    check(!target.importBackup(tampered, &error) && read(target.directory() + "/workspace.json") == original && read(external) == "external bytes", "corrupt restore modified state or original files");
    bad = document; badAssets = bad["backupAssets"].toArray(); entry = badAssets[0].toObject(); entry["path"] = "attachments/CON.txt"; badAssets[0] = entry; bad["backupAssets"] = badAssets; write(tampered, QJsonDocument(bad).toJson());
    check(!target.importBackup(tampered, &error), "Windows reserved asset path accepted");
    return ok ? 0 : 1;
}
