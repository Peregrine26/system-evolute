#include "platform/windows/qt_desktop_bridge.h"

#include <QApplication>

#include <windows.h>

#include <iostream>

namespace {
class TestWindow : public QWidget {
public:
    void release_native_window() { destroy(); }
};
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    TestWindow window;
    crossdesk::QtDesktopBridge bridge(window);
    bool ok = true;
    const auto check = [&](bool condition, const char* description) {
        if (!condition) std::cerr << description << '\n';
        ok &= condition;
    };

    check(window.internalWinId() != 0, "bridge did not create the initial native window");
    // No real HWND, desktop, hotkey registration or DWM is needed. Reproduce
    // the interval after Qt releases its native window and before recreation.
    window.release_native_window();
    check(window.internalWinId() == 0, "native window was not released");

    MSG message{};
    qintptr result = 123;
    for (const auto* type : {"windows_generic_MSG", "windows_dispatcher_MSG"}) {
        message.message = WM_NULL;
        check(!bridge.nativeEventFilter(type, &message, &result), "unrelated message consumed");
        check(window.internalWinId() == 0, "unrelated message recreated the native window");

        message.message = WM_HOTKEY;
        check(!bridge.nativeEventFilter(type, &message, &result), "unregistered hotkey consumed");
        check(window.internalWinId() == 0, "hotkey message recreated the native window");
    }
    check(result == 123, "ignored message changed the native result");

    window.winId();
    check(window.internalWinId() != 0, "explicit recreation failed");
    window.release_native_window();
    check(window.internalWinId() == 0, "bridge prevented native window destruction");
    // The bridge is destroyed before the QWidget, including in this state
    // with no native window. Its destructor must also avoid window creation.
    return ok ? 0 : 1;
}
