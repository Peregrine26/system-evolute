#pragma once

#include "platform/windows/desktop_services.h"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QRect>
#include <QWidget>

#include <functional>
#include <utility>

namespace crossdesk {
struct QtWidgetPreview {
    NativeWindowHandle source = 0;
    // Borrowed for this call only; the widget remains owned by C.
    QWidget* widget = nullptr;
    // Content area in widget-local logical pixels, excluding titles/buttons.
    QRect content;
};

// A-owned Windows/Qt integration. C includes this header without windows.h.
// Keep it on the stack AFTER the main window, or destroy before the window.
// Do not change the main window's native HWND while the bridge is active.
class QtDesktopBridge final : public QObject, public QAbstractNativeEventFilter {
public:
    explicit QtDesktopBridge(QWidget& main_window);
    ~QtDesktopBridge() override;
    DesktopResult register_hotkey();
    void unregister_hotkey();
    DesktopResult hotkey_state() const { return hotkey_state_; }
    // Update C's setAllowHide(result.ready()). This callback must not recreate
    // the native window; defer any show/recreation work to the Qt event loop.
    std::function<void(DesktopResult)> hotkey_state_changed;
    std::function<void()> toggle_overview;
    // Rectangles must be in main-window client coordinates (Qt logical units),
    // mapped from child widgets with mapTo(&main_window, ...).
    std::vector<DesktopResult> set_previews(
        const std::vector<std::pair<NativeWindowHandle, QRect>>& regions);
    // Maps a fully visible child content area to main-window client DIPs.
    // Never calls the child's winId(). Hidden/clipped/fading regions fall back.
    DesktopResult map_preview_region(const QWidget& widget, const QRect& content,
                                     QRect& destination) const;
    std::vector<DesktopResult> set_widget_previews(const std::vector<QtWidgetPreview>& regions);
    void clear_previews();
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    QWidget& window_;
    NativeWindowHandle destination_ = 0;
    DesktopResult hotkey_state_;
    OverviewHotkey hotkey_;
    WindowPreviews previews_;
};
} // namespace crossdesk
