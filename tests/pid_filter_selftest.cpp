#include "desktop/store.h"
#include "desktop/workbench.h"
#include "desktop/window_overview.h"
#include "platform/windows/win32_api_adapter.h"
#include "windows_window_scanner.h"
#include <QApplication>
#include <QDialog>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir directory;
    crossdesk::desktop::Store store(directory.path());
    if (!store.load()) return 1;
    store.addWidget("folder", "Mapped files", {{"path", directory.path()}});
    store.addWidget("todo", "Tasks");
    crossdesk::desktop::Workbench main(store);
    main.setAttribute(Qt::WA_ShowWithoutActivating); main.show();
    QDialog settings(&main), popup(&main);
    settings.setWindowTitle("Settings with an arbitrary title");
    popup.setWindowTitle("File Explorer"); // A self window imitating an external title.
    settings.setAttribute(Qt::WA_ShowWithoutActivating); popup.setAttribute(Qt::WA_ShowWithoutActivating);
    settings.show(); popup.show();
    crossdesk::desktop::WindowOverview overview(&main);
    overview.setAttribute(Qt::WA_ShowWithoutActivating); overview.show(); app.processEvents();
    const auto own = std::uint32_t(QCoreApplication::applicationPid());
    const crossdesk::Win32ApiAdapter api;
    const auto scan = crossdesk::WindowsWindowScanner(api).scan_with_status();
    if (!scan.complete) { std::cerr << "incomplete desktop enumeration\n"; return 1; }
    int self = 0;
    for (const auto& window : scan.windows)
        if (window.process_id == own && window.visible && !window.title.empty()) ++self;
    if (self < 6) { std::cerr << "owned window fixture did not create all window types: " << self << '\n'; return 1; }
    auto* list = overview.findChild<QTreeWidget*>();
    if (!list) return 1;
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        if (list->topLevelItem(i)->data(0, Qt::UserRole + 1).toUInt() == own) {
            std::cerr << "self window leaked into actual overview UI\n"; return 1;
        }
    std::cout << "Owned visible windows excluded: " << self << "; external candidates: " << list->topLevelItemCount() << '\n';
    return 0;
}
