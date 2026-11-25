#include "desktop/global_shortcuts.h"
#include <windows.h>
#include <QApplication>

namespace crossdesk::desktop {
struct GlobalShortcuts::Impl {
    struct Binding { int id; unsigned modifiers; unsigned key; QString label; };
    HWND destination;
    QMap<QString, Binding> active;
};
namespace {
bool decode(const QKeySequence& sequence, unsigned& modifiers, unsigned& key) {
    if (sequence.count() != 1) return false;
    const auto combination = sequence[0]; const auto value = combination.key();
    const auto flags = combination.keyboardModifiers();
    const bool functionKey = value >= Qt::Key_F1 && value <= Qt::Key_F24;
    const int modifierCount = int(flags.testFlag(Qt::ControlModifier)) + int(flags.testFlag(Qt::AltModifier)) + int(flags.testFlag(Qt::ShiftModifier));
    if (!functionKey && !flags.testFlag(Qt::MetaModifier) && modifierCount < 2) return false;
    modifiers = (flags.testFlag(Qt::ControlModifier) ? MOD_CONTROL : 0) |
        (flags.testFlag(Qt::AltModifier) ? MOD_ALT : 0) | (flags.testFlag(Qt::ShiftModifier) ? MOD_SHIFT : 0) |
        (flags.testFlag(Qt::MetaModifier) ? MOD_WIN : 0);
    if ((value >= Qt::Key_A && value <= Qt::Key_Z) || (value >= Qt::Key_0 && value <= Qt::Key_9)) key = unsigned(value);
    else if (value >= Qt::Key_F1 && value <= Qt::Key_F24) key = VK_F1 + unsigned(value - Qt::Key_F1);
    else switch (value) {
        case Qt::Key_Space: key = VK_SPACE; break; case Qt::Key_Tab: key = VK_TAB; break;
        case Qt::Key_Return: key = VK_RETURN; break; case Qt::Key_Escape: key = VK_ESCAPE; break;
        case Qt::Key_Home: key = VK_HOME; break; case Qt::Key_End: key = VK_END; break;
        case Qt::Key_Left: key = VK_LEFT; break; case Qt::Key_Right: key = VK_RIGHT; break;
        case Qt::Key_Up: key = VK_UP; break; case Qt::Key_Down: key = VK_DOWN; break;
        case Qt::Key_Delete: key = VK_DELETE; break; case Qt::Key_Insert: key = VK_INSERT; break;
        default: return false;
    }
    return true;
}
}
GlobalShortcuts::GlobalShortcuts(QWidget& destination) : impl_(std::make_unique<Impl>()) {
    impl_->destination = reinterpret_cast<HWND>(destination.winId());
    qApp->installNativeEventFilter(this);
}
GlobalShortcuts::~GlobalShortcuts() {
    qApp->removeNativeEventFilter(this);
    for (const auto& binding : impl_->active) UnregisterHotKey(impl_->destination, binding.id);
}
QStringList GlobalShortcuts::configure(const QMap<QString, QKeySequence>& bindings) {
    QStringList reports;
    const QStringList roles{"overview", "widgets", "note", "search"};
    for (int i = 0; i < roles.size(); ++i) {
        const auto role = roles[i]; const auto sequence = bindings.value(role);
        auto old = impl_->active.find(role);
        if (sequence.isEmpty()) {
            if (old != impl_->active.end()) { UnregisterHotKey(impl_->destination, old->id); impl_->active.erase(old); }
            continue;
        }
        const auto label = sequence.toString(QKeySequence::NativeText);
        if (old != impl_->active.end() && old->label == label) { reports << label; continue; }
        unsigned modifiers = 0, key = 0;
        if (!decode(sequence, modifiers, key)) { reports << label + QCoreApplication::translate("CrossDesk", "：不支持此组合") +
            (old == impl_->active.end() ? QString{} : QCoreApplication::translate("CrossDesk", "，继续使用 ") + old->label); continue; }
        const int base = 0x5100 + i * 2;
        const int id = old != impl_->active.end() && old->id == base ? base + 1 : base;
        SetLastError(0);
        // Register using a second ID first: a conflict keeps the old binding.
        if (!RegisterHotKey(impl_->destination, id, modifiers | MOD_NOREPEAT, key)) {
            reports << label + QCoreApplication::translate("CrossDesk", "：被占用（%1）").arg(GetLastError()) +
                (old == impl_->active.end() ? QString{} : QCoreApplication::translate("CrossDesk", "，继续使用 ") + old->label);
            continue;
        }
        if (old != impl_->active.end()) UnregisterHotKey(impl_->destination, old->id);
        impl_->active[role] = {id, modifiers, key, label}; reports << label;
    }
    return reports;
}
bool GlobalShortcuts::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
    if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
    const auto* event = static_cast<MSG*>(message);
    if (event->message != WM_HOTKEY || event->hwnd != impl_->destination) return false;
    for (auto it = impl_->active.cbegin(); it != impl_->active.cend(); ++it) {
        if (event->wParam == WPARAM(it->id) && LOWORD(event->lParam) == it->modifiers && HIWORD(event->lParam) == it->key) {
            if (triggered) triggered(it.key()); if (result) *result = 0; return true;
        }
    }
    return false;
}
}
