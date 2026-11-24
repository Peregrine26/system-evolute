#include "platform/windows/qt_desktop_bridge.h"

#include <QApplication>

#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QWidget source_a, source_b, window;
    source_a.resize(320, 200);
    source_b.resize(320, 200);
    window.resize(800, 600);
    QWidget panel(&window), first(&panel), second(&panel);
    panel.setGeometry(20, 80, 740, 460);
    first.setGeometry(10, 10, 340, 400);
    second.setGeometry(380, 10, 340, 400);
    source_a.show();
    source_b.show();
    window.show();
    app.processEvents();
    crossdesk::QtDesktopBridge bridge(window);
    const auto child_id = first.internalWinId();
    const auto requests = [&] {
        return std::vector<crossdesk::QtWidgetPreview>{
            {static_cast<crossdesk::NativeWindowHandle>(source_a.winId()), &first, first.rect()},
            {static_cast<crossdesk::NativeWindowHandle>(source_b.winId()), &second, second.rect()}
        };
    };
    bool ok = true;
    const auto check = [&](bool condition, const char* text) {
        if (!condition) std::cerr << text << '\n';
        ok &= condition;
    };
    auto results = bridge.set_widget_previews(requests());
    check(results.size() == 2 && results[0].ready() && results[1].ready(),
          "two widget previews did not register against the top-level destination");
    check(first.internalWinId() == child_id, "preview registration forced a child HWND");
    first.move(20, 30);
    results = bridge.set_widget_previews(requests());
    check(results[0].ready() && results[1].ready(), "moved widget preview failed");
    first.hide();
    results = bridge.set_widget_previews(requests());
    check(!results[0].ready() && results[1].ready(), "hidden slot broke the other preview");
    first.show();
    source_a.hide();
    results = bridge.set_widget_previews(requests());
    check(!results[0].ready() && results[1].ready(), "hidden source broke the other preview");
    window.hide();
    results = bridge.set_widget_previews(requests());
    check(!results[0].ready() && !results[1].ready(), "hidden overview recreated previews");
    bridge.clear_previews();
    return ok ? 0 : 1;
}
