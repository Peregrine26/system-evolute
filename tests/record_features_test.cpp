#include "desktop/page_factories.h"
#include "desktop/record_actions.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <iostream>
#include <memory>

using namespace crossdesk::desktop;
namespace {
bool check(bool value, const char* message) { if (!value) std::cerr << message << '\n'; return value; }
QString uuid() { return QUuid::createUuid().toString(); }
void settle(QApplication& app) { for (int i = 0; i < 4; ++i) app.processEvents(); }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); QTemporaryDir temporary; Store store(temporary.path()); bool ok = store.load();
    const auto first = uuid(), second = uuid();
    const auto now = QDateTime::fromString("2031-04-10T09:30:00+08:00", Qt::ISODate);
    QJsonArray tasks{QJsonObject{{"id", first}, {"title", "repeat"}, {"done", false}, {"repeat", 1}, {"due", "2031-04-01T09:30:00+08:00"}, {"notes", "**Important**"}},
                     QJsonObject{{"id", second}, {"title", "once"}, {"done", false}, {"repeat", 0}, {"due", "2031-04-11T10:00:00+08:00"}}};
    ok &= store.setRecords("todos", tasks);
    ok &= check(completeTasks(store, {first, second}, true, now), "bulk completion failed");
    ok &= check(store.records("todos").size() == 3 && findRecord(store, "todos", first)["done"].toBool() && findRecord(store, "todos", second)["done"].toBool(), "bulk completion lost selected records");
    const auto next = store.records("todos").last().toObject();
    ok &= check(QDateTime::fromString(next["due"].toString(), Qt::ISODate) >= now && !next["done"].toBool(), "recurrence failed to skip old dates");
    completeTasks(store, {first, second}, true, now); completeTasks(store, {first}, false, now); completeTasks(store, {first}, true, now);
    ok &= check(store.records("todos").size() == 3, "bulk or recheck duplicated recurring occurrence");
    ok &= check(patchRecord(store, "todos", first, {{"color", "#397eb8"}, {"title", "changed"}}) && findRecord(store, "todos", first)["done"].toBool(), "patch overwrote unrelated live task fields");
    const auto external = temporary.path() + "/original.txt"; { QFile file(external); file.open(QIODevice::WriteOnly); file.write("external original"); }
    QString error;
    ok &= check(addRecordAttachments(store, "todos", first, {external}, false, &error), "linked attachment failed");
    ok &= check(removeRecordAttachments(store, "todos", first, {external}, true, &error) && QFile::exists(external), "attachment removal deleted original");
    ok &= check(addRecordAttachments(store, "todos", first, {external}, true, &error), "managed attachment copy failed");
    const auto managed = recordAttachmentPaths(findRecord(store, "todos", first)).first();
    ok &= check(managed != external && QFile::exists(managed), "managed copy did not use separate location");
    patchRecord(store, "todos", second, {{"attachments", QJsonArray{managed}}});
    ok &= check(removeRecordAttachments(store, "todos", first, {managed}, true, &error) && QFile::exists(managed), "shared managed file was deleted");
    ok &= check(removeRecordAttachments(store, "todos", second, {managed}, true, &error) && !QFile::exists(managed) && QFile::exists(external), "unreferenced managed cleanup failed");
    const auto noteId = uuid(); const auto url = QUrl::fromLocalFile(external).toString(QUrl::FullyEncoded);
    store.setRecords("notes", {QJsonObject{{"id", noteId}, {"title", "note"}, {"body", "before [original](" + url + ") after"}}});
    ok &= check(recordAttachmentPaths(findRecord(store, "notes", noteId)).contains(external), "legacy Markdown attachments not detected");
    ok &= check(removeRecordAttachments(store, "notes", noteId, {external}, true, &error) && !findRecord(store, "notes", noteId)["body"].toString().contains("file:") && QFile::exists(external), "Markdown unlink removed external original or retained broken link");
    const auto missingId = uuid(); ok &= check(!addRecordAttachments(store, "todos", missingId, {external}, true, &error), "deleted record accepted attachments");

    auto notify = [](const QString&) {};
    std::unique_ptr<QWidget> todos(createRecordPage(store, "todo", notify)); todos->resize(1000, 700); todos->show(); settle(app);
    auto* todoList = todos->findChild<QListWidget*>("todoList");
    auto* preview = todos->findChild<QTextBrowser*>("todoMarkdownPreview");
    ok &= check(todoList && preview && todoList->selectionMode() == QAbstractItemView::ExtendedSelection && preview->isVisible(), "wide todo detail layout missing");
    todoList->setCurrentRow(0); settle(app); ok &= check(preview->toPlainText().contains("Important"), "todo Markdown detail missing");
    todoList->selectAll(); todos->findChild<QPushButton*>("uncompleteSelectedTodos")->click(); settle(app);
    ok &= check(!findRecord(store, "todos", first)["done"].toBool() && !findRecord(store, "todos", second)["done"].toBool(), "bulk UI action failed");
    todos->resize(500, 700); settle(app); auto* todoToggle = todos->findChild<QPushButton*>("todoPaneToggle");
    ok &= check(todoToggle && todoToggle->isVisible() && !preview->isVisible(), "narrow todo pane did not switch to list mode");
    todoToggle->click(); settle(app); ok &= check(preview->isVisible() && !todoList->isVisible(), "narrow todo details navigation failed");

    std::unique_ptr<QWidget> notes(createRecordPage(store, "note", notify)); notes->resize(1000, 650); notes->show(); settle(app);
    auto* noteList = notes->findChild<QListWidget*>("noteList"); auto* editor = notes->findChild<QPlainTextEdit*>("noteEditor");
    auto* pin = notes->findChild<QCheckBox*>("pinNote"); auto* paper = notes->findChild<QComboBox*>("notePaper"); auto* focus = notes->findChild<QCheckBox*>("noteFocus");
    ok &= check(noteList && editor && pin && paper && focus, "note feature controls missing");
    pin->setChecked(true); paper->setCurrentIndex(1); settle(app);
    ok &= check(findRecord(store, "notes", noteId)["pinned"].toBool() && findRecord(store, "notes", noteId)["paper"].toInt() == 1 && noteList->item(0)->text().startsWith(QStringLiteral("★")), "pin/paper persistence failed");
    focus->setChecked(true); settle(app); ok &= check(!noteList->isVisible() && editor->isVisible(), "note focus mode did not hide list");
    focus->setChecked(false); settle(app); ok &= check(noteList->isVisible(), "note focus mode did not restore list");
    editor->setPlainText("edited pending text"); flushRecordPages(notes.get());
    ok &= check(findRecord(store, "notes", noteId)["body"].toString() == "edited pending text", "note flush lost edits");
    // Two independent note widgets must preserve both concurrent edits.
    std::unique_ptr<QWidget> other(createRecordPage(store, "note", notify));
    auto* otherEditor = other->findChild<QPlainTextEdit*>("noteEditor");
    editor->setPlainText("first window edit"); otherEditor->setPlainText("second window edit");
    flushRecordPages(notes.get()); flushRecordPages(other.get());
    bool one = false, two = false; for (const auto& value : store.records("notes")) { one |= value.toObject()["body"] == "first window edit"; two |= value.toObject()["body"] == "second window edit"; }
    ok &= check(one && two && store.records("notes").size() == 2, "concurrent note edits overwrote each other");
    Store reloaded(temporary.path()); ok &= check(reloaded.load() && findRecord(reloaded, "notes", noteId)["pinned"].toBool(), "record options lost on reload");
    other.reset(); notes.reset(); todos.reset();
    return ok ? 0 : 1;
}
