#pragma once

#include "platform/windows/desktop_services.h"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QRect>
#include <QWidget>

#include <functional>
#include <utility>

namespace crossdesk {
// A-owned Windows/Qt integration. C includes this header without windows.h.
// Keep it on the stack AFTER the main window, or destroy before the window.
// Do not change the main window's native HWND while the bridge is active.
class QtDesktopBridge final : public QObject, public QAbstractNativeEventFilter {
public:
    explicit QtDesktopBridge(QWidget& main_window);
    ~QtDesktopBridge() override;
    DesktopResult register_hotkey();
    std::function<void()> toggle_overview;
    // Rectangles must be in main-window client coordinates (Qt logical units),
    // mapped from child widgets with mapTo(&main_window, ...).
    std::vector<DesktopResult> set_previews(
        const std::vector<std::pair<NativeWindowHandle, QRect>>& regions);
    void clear_previews();
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    QWidget& window_;
    OverviewHotkey hotkey_;
    WindowPreviews previews_;
};
} // namespace crossdesk
