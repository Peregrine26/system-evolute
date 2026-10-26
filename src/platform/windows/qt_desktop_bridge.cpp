#include "platform/windows/qt_desktop_bridge.h"

#include <windows.h>

#include <QApplication>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QRegion>
#include <QtMath>

namespace crossdesk {
QtDesktopBridge::QtDesktopBridge(QWidget& main_window) : window_(main_window) {
    // winId forces creation of the top-level native destination.
    destination_ = static_cast<NativeWindowHandle>(window_.winId());
    qApp->installNativeEventFilter(this);
    window_.installEventFilter(this);
}
QtDesktopBridge::~QtDesktopBridge() {
    window_.removeEventFilter(this);
    qApp->removeNativeEventFilter(this);
    previews_.clear();
    hotkey_.reset();
}
DesktopResult QtDesktopBridge::register_hotkey() {
    hotkey_state_ = hotkey_.register_for(destination_);
    if (hotkey_state_changed) hotkey_state_changed(hotkey_state_);
    return hotkey_state_;
}
void QtDesktopBridge::unregister_hotkey() {
    hotkey_.reset();
    hotkey_state_ = {DesktopStatus::Unavailable, 0};
    if (hotkey_state_changed) hotkey_state_changed(hotkey_state_);
}
std::vector<DesktopResult> QtDesktopBridge::set_previews(
    const std::vector<std::pair<NativeWindowHandle, QRect>>& regions) {
    if (!destination_ || !window_.isVisible() || window_.isMinimized() ||
        window_.windowOpacity() < 1.0) {
        previews_.clear();
        return std::vector<DesktopResult>(regions.size(), {DesktopStatus::Unavailable, 0});
    }
    std::vector<PreviewRequest> requests;
    const qreal ratio = window_.devicePixelRatioF();
    for (const auto& slot : regions) {
        const auto& rect = slot.second;
        requests.push_back({slot.first,
            {qRound(rect.x() * ratio), qRound(rect.y() * ratio),
             qRound((rect.x() + rect.width()) * ratio),
             qRound((rect.y() + rect.height()) * ratio)}});
    }
    return previews_.replace(destination_, requests);
}
DesktopResult QtDesktopBridge::map_preview_region(
    const QWidget& widget, const QRect& content, QRect& destination) const {
    destination = {};
    if (!window_.isWindow() || widget.window() != &window_) {
        return {DesktopStatus::InvalidWindow, 0};
    }
    if (!content.isValid() || !widget.rect().contains(content)) {
        return {DesktopStatus::InvalidRectangle, 0};
    }
    if (!window_.isVisible() || window_.isMinimized() ||
        window_.windowOpacity() < 1.0 || !widget.isVisibleTo(&window_)) {
        return {DesktopStatus::Unavailable, 0};
    }
    // DWM is composed outside Qt's paint/effect pipeline. Do not paint over
    // a fade or over portions occluded by another card/title/control.
    for (const QWidget* parent = &widget; parent; parent = parent->parentWidget()) {
        if (const auto* effect = parent->graphicsEffect(); effect && effect->isEnabled()) {
            const auto* opacity = qobject_cast<const QGraphicsOpacityEffect*>(effect);
            if (!opacity || opacity->opacity() < 1.0) {
                return {DesktopStatus::Unavailable, 0};
            }
        }
        if (parent == &window_) break;
    }
    if (!QRegion(content).subtracted(widget.visibleRegion()).isEmpty()) {
        return {DesktopStatus::Unavailable, 0};
    }
    // visibleRegion() can retain areas behind non-opaque Qt siblings. DWM
    // would still cover their custom-painted text/cards, so conservatively
    // reject intersections with any visible widget above the content.
    for (const auto* child : widget.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (child->isVisible() && !child->isWindow() && child->geometry().intersects(content)) {
            return {DesktopStatus::Unavailable, 0};
        }
    }
    for (const QWidget* node = &widget; node != &window_; node = node->parentWidget()) {
        const QWidget* parent = node->parentWidget();
        const QRect area(widget.mapTo(parent, content.topLeft()), content.size());
        bool above = false;
        // QWidget raise/lower maintains stacking order in QObject::children().
        for (const auto* object : parent->children()) {
            if (object == node) { above = true; continue; }
            const auto* sibling = qobject_cast<const QWidget*>(object);
            if (above && sibling && !sibling->isWindow() && sibling->isVisible() &&
                sibling->geometry().intersects(area)) {
                return {DesktopStatus::Unavailable, 0};
            }
        }
    }
    destination = QRect(widget.mapTo(&window_, content.topLeft()), content.size());
    return {DesktopStatus::Ready, 0};
}
std::vector<DesktopResult> QtDesktopBridge::set_widget_previews(
    const std::vector<QtWidgetPreview>& regions) {
    std::vector<DesktopResult> results(regions.size(), {DesktopStatus::InvalidWindow, 0});
    std::vector<std::pair<NativeWindowHandle, QRect>> valid;
    std::vector<std::size_t> positions;
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const auto& region = regions[i];
        if (!region.widget) continue;
        QRect mapped;
        results[i] = map_preview_region(*region.widget, region.content, mapped);
        if (results[i].ready()) {
            valid.push_back({region.source, mapped});
            positions.push_back(i);
        }
    }
    // One replace also clears relationships belonging to now-invalid regions.
    const auto registered = set_previews(valid);
    for (std::size_t i = 0; i < positions.size(); ++i) results[positions[i]] = registered[i];
    return results;
}
void QtDesktopBridge::clear_previews() { previews_.clear(); }
bool QtDesktopBridge::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
    if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
    const auto* msg = static_cast<MSG*>(message);
    // winId() can create a platform window. Calling it inside a native callback
    // during destruction/recreation recursively creates more windows.
    if (msg->message != WM_HOTKEY) return false;
    if (reinterpret_cast<NativeWindowHandle>(msg->hwnd) !=
        destination_) return false;
    if (!hotkey_.handle_message(msg->message, msg->wParam, msg->lParam)) return false;
    if (toggle_overview) toggle_overview();
    if (result) *result = 0;
    return true;
}
bool QtDesktopBridge::eventFilter(QObject* object, QEvent* event) {
    if (object == &window_ && event->type() == QEvent::WinIdChange) {
        previews_.clear();
        // internalWinId observes without creating a new native window.
        destination_ = static_cast<NativeWindowHandle>(window_.internalWinId());
        unregister_hotkey();
    }
    if (object == &window_ && (event->type() == QEvent::Hide ||
                              event->type() == QEvent::Close)) previews_.clear();
    return QObject::eventFilter(object, event);
}
} // namespace crossdesk
