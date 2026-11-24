#include "desktop/pages.h"
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QListView>
#include <QMimeData>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <memory>

using namespace crossdesk::desktop;
bool check(bool value, const char* message) { if (!value) std::cerr << message << '\n'; return value; }
int main(int argc, char** argv) {
    QApplication app(argc, argv); QTemporaryDir root; Store store(root.path());
    bool ok = store.load(); auto notify = [](const QString&) {};
    const auto todoId = store.addWidget("todo", "tasks");
    const auto noteId = store.addWidget("note", "notes");
    std::unique_ptr<QWidget> todos(createPage(store, todoId, notify));
    auto* title = todos->findChild<QLineEdit*>("todoTitle");
    auto* repeat = todos->findChild<QComboBox*>("todoRecurrence");
    auto* list = todos->findChild<QListWidget*>("todoList");
    auto* due = todos->findChild<QDateTimeEdit*>("todoDue");
    auto* reminder = todos->findChild<QCheckBox*>("todoReminder");
    ok &= check(title && repeat && list, "todo UI controls missing");
    if (!ok) return 1;
    repeat->setCurrentIndex(1); title->setText(QStringLiteral("中文待办"));
    due->setDateTime(QDateTime::fromString("2031-04-02T09:10:00+08:00", Qt::ISODate)); reminder->setChecked(true);
    QMetaObject::invokeMethod(title, "returnPressed", Qt::DirectConnection); app.processEvents();
    ok &= check(store.records("todos").size() == 1 && title->text().isEmpty(), "UI task creation did not persist");
    list->item(0)->setCheckState(Qt::Checked); app.processEvents();
    ok &= check(store.records("todos").size() == 2 && store.records("todos")[0].toObject()["done"].toBool(), "daily recurrence did not create next task");
    list->item(0)->setCheckState(Qt::Unchecked); app.processEvents();
    list->item(0)->setCheckState(Qt::Checked); app.processEvents();
    ok &= check(store.records("todos").size() == 2, "rechecking a completed occurrence duplicated the next task");
    list->setCurrentRow(0);
    const auto taskId = store.records("todos")[0].toObject()["id"].toString();
    const auto editedDue = QDateTime::fromString("2032-07-08T10:20:00+08:00", Qt::ISODate);
    bool edited = false;
    QTimer::singleShot(0, &app, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* editTitle = dialog->findChild<QLineEdit*>("editTodoTitle");
        auto* editDue = dialog->findChild<QDateTimeEdit*>("editTodoDue");
        auto* editReminder = dialog->findChild<QCheckBox*>("editTodoReminder");
        auto* buttons = dialog->findChild<QDialogButtonBox*>("editTodoButtons");
        if (!editTitle || !editDue || !editReminder || !buttons) { dialog->reject(); return; }
        editTitle->setText(QStringLiteral("已编辑标题")); editDue->setDateTime(editedDue); editReminder->setChecked(false);
        edited = true; buttons->button(QDialogButtonBox::Save)->click();
    });
    todos->findChild<QPushButton*>("editTodo")->click(); app.processEvents();
    const auto editedTask = store.records("todos")[0].toObject();
    ok &= check(edited && editedTask["title"].toString() == QStringLiteral("已编辑标题") && !editedTask["reminder"].toBool() &&
                QDateTime::fromString(editedTask["due"].toString(), Qt::ISODate).toMSecsSinceEpoch() == editedDue.toMSecsSinceEpoch() &&
                editedTask["done"].toBool(), "task edit lost deadline/reminder/completion state");
    list->setCurrentRow(0); todos->findChild<QPushButton*>("deleteTodo")->click(); app.processEvents();
    ok &= check(store.records("todos").size() == 1 && store.records("todos")[0].toObject()["id"].toString() != taskId, "task deletion failed");
    {
        std::unique_ptr<QWidget> notes(createPage(store, noteId, notify));
        auto* create = notes->findChild<QPushButton*>("newNote");
        auto* editor = notes->findChild<QPlainTextEdit*>("noteEditor");
        ok &= check(create && editor, "note controls missing");
        if (!create || !editor) return 1;
        create->click(); editor->setPlainText(QStringLiteral("# 第一条\n\nMarkdown 正文"));
        create->click(); // Flush previous record before switching to the new one.
        ok &= check(editor->toPlainText().isEmpty(), "new note copied previous editor contents");
        editor->setPlainText(QStringLiteral("第二条随记"));
    } // Destruction must flush the pending debounce timer.
    const auto notes = store.records("notes");
    ok &= check(notes.size() == 2 && notes[0].toObject()["body"].toString().contains("Markdown") &&
                notes[1].toObject()["body"].toString() == QStringLiteral("第二条随记"), "note text lost during switch/close");
    Store reload(root.path());
    ok &= check(reload.load() && reload.records("notes") == notes, "notes did not survive restart");

    // Real QFileSystemModel drop path, using only files owned by this fixture.
    const auto folder = root.path() + "/mapped"; QDir().mkpath(folder + "/child");
    const auto source = root.path() + "/drag.txt";
    { QFile file(source); file.open(QIODevice::WriteOnly); file.write("drag payload"); }
    const auto folderId = store.addWidget("folder", "mapped", {{"path", folder}});
    std::unique_ptr<QWidget> files(createPage(store, folderId, notify));
    auto* view = files->findChild<QListView*>("folderView");
    auto* model = qobject_cast<QFileSystemModel*>(view->model());
    QMimeData drop; drop.setUrls({QUrl::fromLocalFile(source)});
    ok &= check(model->dropMimeData(&drop, Qt::CopyAction, -1, 0, model->index(folder)) && QFile::exists(source) && QFile::exists(folder + "/drag.txt"),
                "file widget drag/drop did not copy into the mapped folder");
    ok &= check(!model->dropMimeData(&drop, Qt::CopyAction, -1, 0, model->index(folder)), "drag/drop replaced an existing file");
    QMetaObject::invokeMethod(view, "doubleClicked", Qt::DirectConnection, Q_ARG(QModelIndex, model->index(folder + "/child")));
    ok &= check(store.widget(folderId)["data"].toObject()["path"].toString() == folder, "browsing a child changed the mapped root");
    const auto other = root.path() + "/other"; QDir().mkpath(other);
    auto* mapped = files->findChild<QLineEdit*>("folderPath"); mapped->setText(other);
    QMetaObject::invokeMethod(mapped, "returnPressed", Qt::DirectConnection);
    ok &= check(store.widget(folderId)["data"].toObject()["path"].toString() == other, "explicit folder remapping failed");
    bool previewAccepted = false;
    QTimer::singleShot(0, &app, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog) return;
        auto* buttons = dialog->findChild<QDialogButtonBox*>("organizationButtons");
        if (!buttons) { dialog->reject(); return; }
        previewAccepted = true; buttons->button(QDialogButtonBox::Apply)->click();
    });
    organizeFolder(store, folder, files.get(), notify);
    ok &= check(previewAccepted && QFile::exists(folder + QStringLiteral("/文档/drag.txt")) && !QFile::exists(folder + "/drag.txt") &&
                !store.records("operations").isEmpty(), "organization preview/real move/history failed");
    files.reset();
    todos.reset();
    return ok ? 0 : 1;
}
