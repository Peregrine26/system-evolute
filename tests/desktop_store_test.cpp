#include "desktop/store.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>

using namespace crossdesk::desktop;
bool check(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}
QByteArray read(const QString& file) { QFile input(file); input.open(QIODevice::ReadOnly); return input.readAll(); }
bool write(const QString& file, const QByteArray& bytes) {
    QFile output(file); return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size();
}
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv); QTemporaryDir root; bool ok = root.isValid();
    Store store(root.path() + "/profile"); QString error;
    ok &= check(store.load(&error), "fresh workspace did not load");
    const auto id = store.addWidget("folder", QStringLiteral("学习文件"), {{"path", root.path()}});
    ok &= check(!id.isEmpty(), "widget creation failed");
    Store reopened(store.directory());
    ok &= check(reopened.load() && reopened.widget(id)["title"].toString() == QStringLiteral("学习文件"), "stable identity/Unicode did not survive reload");
    auto invalid = store.widget(id); invalid["height"] = 0;
    ok &= check(!store.putWidget(invalid) && store.widget(id)["height"].toInt() > 0, "invalid update changed live state");
    const auto backup = root.path() + "/backup.json";
    ok &= check(store.exportBackup(backup), "backup export failed");
    auto config = store.widget(id); config["title"] = "changed";
    ok &= check(store.putWidget(config) && store.importBackup(backup) && store.widget(id)["title"] != "changed", "backup restore failed");
    const auto original = read(store.directory() + "/workspace.json");
    auto bad = QJsonDocument::fromJson(original).object(); auto widgets = bad["widgets"].toArray(); widgets.append(widgets.first()); bad["widgets"] = widgets;
    const auto tampered = root.path() + "/tampered.json"; write(tampered, QJsonDocument(bad).toJson());
    ok &= check(!store.importBackup(tampered) && read(store.directory() + "/workspace.json") == original,
                "duplicate identity backup overwrote valid state");
    config = store.widget(id); config["title"] = "second"; store.putWidget(config);
    write(store.directory() + "/workspace.json", "broken json");
    Store recovery(store.directory());
    ok &= check(recovery.load() && recovery.widget(id)["title"].toString() == QStringLiteral("学习文件"), "last valid state was not recovered");

    const auto source = root.path() + QStringLiteral("/源文件.bin");
    const QByteArray payload(150000, '\x5a'); write(source, payload);
    const auto destination = root.path() + "/copy.bin";
    ok &= check(copyPath(source, destination).ok && read(destination) == payload && read(source) == payload, "verified file copy failed");
    write(destination, "existing");
    ok &= check(!copyPath(source, destination).ok && read(destination) == "existing", "copy replaced an existing target");
    ok &= check(!movePath(source, destination).ok && QFile::exists(source), "failed move deleted source");
    ok &= check(!copyPath(source, source).ok, "copy to self accepted");
    const auto folder = root.path() + "/folder"; QDir().mkpath(folder + "/nested"); write(folder + "/nested/data.txt", "nested payload");
    ok &= check(!copyPath(folder, folder + "/child").ok, "recursive self-copy accepted");
    ok &= check(copyPath(folder, root.path() + "/folder-copy").ok && read(root.path() + "/folder-copy/nested/data.txt") == "nested payload", "directory copy failed");
    const auto moved = root.path() + "/moved.bin";
    ok &= check(movePath(source, moved).ok && !QFile::exists(source) && read(moved) == payload, "successful move failed");
    ok &= check(recordMove(store, source, moved), "move history not recorded");
    ok &= check(movePath(moved, source).ok && read(source) == payload, "history reversal failed");
    ok &= check(categoryFor("photo.JPG") == QStringLiteral("图片") && categoryFor("paper.pdf") == QStringLiteral("文档"), "routing categories failed");
    ok &= check(!copyPath(root.path() + "/missing", root.path() + "/output").ok, "missing source accepted");
    const auto now = QDateTime::fromString("2030-01-02T12:00:00+08:00", Qt::ISODate);
    auto task = QJsonObject{{"id", QUuid::createUuid().toString()}, {"title", "due"}, {"due", now.toString(Qt::ISODate)}, {"done", false}, {"reminder", true}};
    QJsonArray tasks{task}; const auto reminders = dueReminders(tasks, now);
    ok &= check(reminders.size() == 1 && dueReminders(tasks, now.addSecs(-1)).isEmpty(), "deadline boundary reminder failed");
    QSet<QString> acknowledged; acknowledged.insert(reminders.first().key);
    ok &= check(dueReminders(tasks, now, acknowledged).isEmpty(), "same deadline reminder repeated");
    task["due"] = now.addSecs(-10).toString(Qt::ISODate);
    ok &= check(dueReminders({task}, now, acknowledged).size() == 1, "rescheduled deadline remained incorrectly acknowledged");
    task["done"] = true;
    ok &= check(dueReminders({task}, now).isEmpty(), "completed task still reminded");
    task["done"] = false; task["reminder"] = false;
    ok &= check(dueReminders({task}, now).isEmpty(), "disabled reminder still fired");
    task["reminder"] = true; task["due"] = "invalid";
    ok &= check(dueReminders({task}, now).isEmpty(), "invalid deadline fired");
    task["due"] = now.toString(Qt::ISODate); task["notes"] = "body";
    const auto attachment = store.directory() + "/attachments/fixture/payload.txt";
    QDir().mkpath(QFileInfo(attachment).absolutePath()); write(attachment, "attachment bytes");
    task["attachments"] = QJsonArray{attachment}; store.setRecords("todos", {task});
    const auto portable = root.path() + "/portable.json";
    ok &= check(store.exportBackup(portable), "portable export failed");
    QFile::remove(attachment);
    ok &= check(store.importBackup(portable), "portable restore failed");
    const auto restored = store.records("todos").first().toObject()["attachments"].toArray().first().toString();
    ok &= check(restored != attachment && read(restored) == "attachment bytes", "attachment was not restored/rebased");
    auto malicious = QJsonDocument::fromJson(read(portable)).object(); auto assets = malicious["backupAssets"].toArray();
    auto asset = assets[0].toObject(); asset["path"] = "attachments/../../escape.txt"; assets[0] = asset; malicious["backupAssets"] = assets;
    write(tampered, QJsonDocument(malicious).toJson());
    const auto before = read(store.directory() + "/workspace.json");
    ok &= check(!store.importBackup(tampered) && read(store.directory() + "/workspace.json") == before, "unsafe backup path changed state");
    const auto noteId = store.convertRecord("todos", "notes", task["id"].toString());
    ok &= check(!noteId.isEmpty() && store.records("todos").isEmpty() && store.records("notes").first().toObject()["body"] == "body", "atomic task/note conversion failed");
    const auto taskAgain = store.convertRecord("notes", "todos", noteId);
    ok &= check(!taskAgain.isEmpty() && store.records("notes").isEmpty() && store.records("todos").size() == 1, "reverse note/task conversion failed");
    return ok ? 0 : 1;
}
