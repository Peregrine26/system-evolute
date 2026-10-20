#include "platform/windows/qt_desktop_bridge.h"

#include <windows.h>

#include <QApplication>
#include <QEvent>
#include <QtMath>

namespace crossdesk {
QtDesktopBridge::QtDesktopBridge(QWidget& main_window) : window_(main_window) {
    // winId forces creation of the top-level native destination.
    window_.winId();
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
    return hotkey_.register_for(static_cast<NativeWindowHandle>(window_.winId()));
}
std::vector<DesktopResult> QtDesktopBridge::set_previews(
    const std::vector<std::pair<NativeWindowHandle, QRect>>& regions) {
    std::vector<PreviewRequest> requests;
    const qreal ratio = window_.devicePixelRatioF();
    for (const auto& slot : regions) {
        const auto& rect = slot.second;
        requests.push_back({slot.first,
            {qRound(rect.x() * ratio), qRound(rect.y() * ratio),
             qRound((rect.x() + rect.width()) * ratio),
             qRound((rect.y() + rect.height()) * ratio)}});
    }
    return previews_.replace(static_cast<NativeWindowHandle>(window_.winId()), requests);
}
void QtDesktopBridge::clear_previews() { previews_.clear(); }
bool QtDesktopBridge::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
    if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
    const auto* msg = static_cast<MSG*>(message);
    if (reinterpret_cast<NativeWindowHandle>(msg->hwnd) !=
        static_cast<NativeWindowHandle>(window_.winId())) return false;
    if (!hotkey_.handle_message(msg->message, msg->wParam, msg->lParam)) return false;
    if (toggle_overview) toggle_overview();
    if (result) *result = 0;
    return true;
}
bool QtDesktopBridge::eventFilter(QObject* object, QEvent* event) {
    if (object == &window_ && (event->type() == QEvent::Hide ||
                              event->type() == QEvent::Close)) previews_.clear();
    return QObject::eventFilter(object, event);
}
} // namespace crossdesk
