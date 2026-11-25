#include "desktop/workbench.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <iostream>

using namespace crossdesk::desktop;
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QTemporaryDir root; Store store(root.path()); bool ok = store.load();
    auto check = [&](bool value, const char* message) { if (!value) { ok = false; std::cerr << message << '\n'; } };
    const auto noteId = store.addWidget("note", "navigation note");
    const auto taskId = store.addWidget("todo", "navigation task");
    Workbench shell(store); int opened = 0, toggled = 0, hidden = 0;
    shell.openWindowOverview = [&] { ++opened; shell.hide(); };
    shell.toggleWindowOverview = [&] { ++toggled; };
    shell.hideWindowOverview = [&] { ++hidden; };
    shell.showPrimaryPanel(); shell.shortcutAction("overview");
    check(opened == 1 && toggled == 1 && !shell.isVisible(), "overview commands opened the tools dashboard");
    shell.showDesktopTools(); check(shell.isVisible() && hidden == 1, "tools navigation did not expose the dedicated panel");
    QDialog modal(&shell); modal.setWindowModality(Qt::ApplicationModal); modal.show(); app.processEvents();
    shell.shortcutAction("overview"); shell.showPrimaryPanel();
    check(opened == 1 && toggled == 1 && modal.isVisible(), "global commands stole focus from modal edit");
    modal.hide(); app.processEvents();
    QWidget *note = nullptr, *todo = nullptr;
    for (auto* window : app.topLevelWidgets()) if (window->objectName() == "desktopWidget") {
        if (window->windowTitle() == "navigation note") note = window;
        if (window->windowTitle() == "navigation task") todo = window;
    }
    check(note && todo, "widget fixture missing"); if (!note || !todo) return 1;
    auto* create = note->findChild<QPushButton*>("newNote"); auto* editor = note->findChild<QPlainTextEdit*>("noteEditor");
    create->click(); editor->setPlainText("Pending note survives widget visibility toggle");
    shell.shortcutAction("widgets"); check(!note->isVisible() && !todo->isVisible(), "F7 did not hide own widgets");
    check(!store.widget(noteId)["hidden"].toBool() && !store.widget(taskId)["hidden"].toBool(), "temporary hide changed persistent close state");
    shell.shortcutAction("widgets"); check(note->isVisible() && todo->isVisible() && editor->toPlainText().contains("survives"), "F7 rebuilt or lost editor state");
    note->close(); shell.shortcutAction("widgets"); shell.shortcutAction("widgets");
    check(!note->isVisible() && todo->isVisible() && store.widget(noteId)["hidden"].toBool(), "F7 reopened an explicitly closed widget");
    shell.shortcutAction("note");
    check(note->isVisible() && !store.widget(noteId)["hidden"].toBool(), "explicit note action did not reopen its widget");
    QAction* showAll = nullptr;
    for (auto* action : shell.menuBar()->findChildren<QAction*>()) if (action->text() == QStringLiteral("显示全部格子")) { showAll = action; break; }
    note->close(); check(store.widget(noteId)["hidden"].toBool(), "widget close was not saved");
    if (showAll) { showAll->trigger(); app.processEvents(); }
    check(showAll && !store.widget(noteId)["hidden"].toBool(), "explicit show-all failed to restore closed widgets");
    // A disk failure must keep the live editor, even when an action requests a rebuild.
    QPointer<QWidget> pending;
    for (auto* window : app.topLevelWidgets()) if (window->objectName() == "desktopWidget" && window->windowTitle() == "navigation note") pending = window;
    if (pending) {
        auto* pendingEditor = pending->findChild<QPlainTextEdit*>("noteEditor");
        pending->findChild<QPushButton*>("newNote")->click();
        pendingEditor->setPlainText("Unsaved edit must remain available after storage failure");
        QFile::remove(store.directory() + "/workspace.json"); QDir().mkdir(store.directory() + "/workspace.json");
        showAll->trigger();
        check(pending && pendingEditor->toPlainText().contains("Unsaved edit"), "failed persistence destroyed the live editor during rebuild");
        pending->findChild<QPushButton*>("newNote")->click();
        check(pendingEditor->toPlainText().contains("Unsaved edit"), "new-note action discarded an edit after save failure");
    } else check(false, "rebuilt note fixture missing");
    return ok ? 0 : 1;
}
