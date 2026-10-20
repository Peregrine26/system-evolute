#include "platform/windows/desktop_services.h"

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>

namespace crossdesk {
namespace {
constexpr int hotkey_id = 0x4344;
HWND hwnd(NativeWindowHandle handle) { return reinterpret_cast<HWND>(handle); }
bool own_top_level(HWND window) {
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    return IsWindow(window) && GetAncestor(window, GA_ROOT) == window &&
           process == GetCurrentProcessId() && thread == GetCurrentThreadId();
}
DesktopResult dwm_result(HRESULT hr) {
    return {SUCCEEDED(hr) ? DesktopStatus::Ready : DesktopStatus::Unavailable,
            static_cast<std::uint32_t>(hr)};
}
}

struct OverviewHotkey::Impl { HWND destination = nullptr; };
OverviewHotkey::OverviewHotkey() : impl_(std::make_unique<Impl>()) {}
OverviewHotkey::~OverviewHotkey() { reset(); }
void OverviewHotkey::reset() {
    if (impl_->destination) UnregisterHotKey(impl_->destination, hotkey_id);
    impl_->destination = nullptr;
}
DesktopResult OverviewHotkey::register_for(NativeWindowHandle destination) {
    reset();
    const auto target = hwnd(destination);
    if (!own_top_level(target)) return {DesktopStatus::InvalidWindow, 0};
    SetLastError(0);
    if (!RegisterHotKey(target, hotkey_id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M')) {
        return {DesktopStatus::Unavailable, GetLastError()};
    }
    impl_->destination = target;
    return {DesktopStatus::Ready, 0};
}
bool OverviewHotkey::handle_message(std::uint32_t message, std::uintptr_t wparam,
                                     std::intptr_t lparam) const {
    return impl_->destination && message == WM_HOTKEY && wparam == hotkey_id &&
           LOWORD(lparam) == (MOD_CONTROL | MOD_ALT) && HIWORD(lparam) == 'M';
}

struct WindowPreviews::Impl {
    struct Entry {
        HWND source;
        HWND destination;
        DWORD process;
        DWORD thread;
        HTHUMBNAIL thumbnail;
    };
    std::vector<Entry> entries;
};
WindowPreviews::WindowPreviews() : impl_(std::make_unique<Impl>()) {}
WindowPreviews::~WindowPreviews() { clear(); }
void WindowPreviews::clear() {
    for (const auto& entry : impl_->entries) DwmUnregisterThumbnail(entry.thumbnail);
    impl_->entries.clear();
}
void WindowPreviews::refresh() {
    auto& entries = impl_->entries;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](const auto& entry) {
        DWORD process = 0;
        const DWORD thread = GetWindowThreadProcessId(entry.source, &process);
        if (!IsWindow(entry.source) || !IsWindow(entry.destination) ||
            process != entry.process || thread != entry.thread) {
            DwmUnregisterThumbnail(entry.thumbnail);
            return true;
        }
        return false;
    }), entries.end());
}
std::vector<DesktopResult> WindowPreviews::replace(
    NativeWindowHandle destination, const std::vector<PreviewRequest>& requests) {
    clear();
    std::vector<DesktopResult> results;
    const auto target = hwnd(destination);
    for (const auto& request : requests) {
        const auto source = hwnd(request.source);
        if (!own_top_level(target) || !IsWindow(source) || source == target ||
            GetAncestor(source, GA_ROOT) != source) {
            results.push_back({DesktopStatus::InvalidWindow, 0});
            continue;
        }
        RECT client{};
        const auto& box = request.destination;
        if (!GetClientRect(target, &client) || box.left < 0 || box.top < 0 ||
            box.right <= box.left || box.bottom <= box.top ||
            box.right > client.right || box.bottom > client.bottom) {
            results.push_back({DesktopStatus::InvalidRectangle, 0});
            continue;
        }
        HTHUMBNAIL thumbnail = nullptr;
        HRESULT hr = DwmRegisterThumbnail(target, source, &thumbnail);
        if (FAILED(hr)) { results.push_back(dwm_result(hr)); continue; }
        SIZE size{};
        hr = DwmQueryThumbnailSourceSize(thumbnail, &size);
        if (SUCCEEDED(hr) && (size.cx <= 0 || size.cy <= 0)) hr = E_INVALIDARG;
        if (SUCCEEDED(hr)) {
            // Fit to slot while preserving aspect ratio. No UI overlaps the slot.
            const double scale = std::min(double(box.right - box.left) / size.cx,
                                          double(box.bottom - box.top) / size.cy);
            const int width = std::max(1, int(size.cx * scale));
            const int height = std::max(1, int(size.cy * scale));
            const int left = box.left + (box.right - box.left - width) / 2;
            const int top = box.top + (box.bottom - box.top - height) / 2;
            DWM_THUMBNAIL_PROPERTIES properties{};
            properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE |
                                 DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
            properties.rcDestination = {left, top, left + width, top + height};
            properties.fVisible = TRUE;
            properties.opacity = 255;
            properties.fSourceClientAreaOnly = FALSE;
            hr = DwmUpdateThumbnailProperties(thumbnail, &properties);
        }
        if (FAILED(hr)) DwmUnregisterThumbnail(thumbnail);
        else {
            DWORD process = 0;
            const DWORD thread = GetWindowThreadProcessId(source, &process);
            impl_->entries.push_back({source, target, process, thread, thumbnail});
        }
        results.push_back(dwm_result(hr));
    }
    return results;
}
} // namespace crossdesk
