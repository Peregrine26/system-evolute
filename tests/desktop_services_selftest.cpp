#include "platform/windows/desktop_services.h"

#include <windows.h>

#include <iostream>

using namespace crossdesk;
bool check(bool condition, const char* text) {
    if (!condition) std::cerr << text << '\n';
    return condition;
}
int main() {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"CrossDeskDesktopServiceTest";
    if (!RegisterClassW(&cls)) return 1;
    auto create = [&] {
        return CreateWindowExW(0, cls.lpszClassName, L"CrossDesk own test window",
            WS_OVERLAPPEDWINDOW, 40, 40, 600, 400, nullptr, nullptr, instance, nullptr);
    };
    HWND destination = create();
    HWND source = create();
    HWND other = create();
    const auto native = [](HWND w) { return reinterpret_cast<NativeWindowHandle>(w); };
    bool ok = destination && source && other;
    if (ok) {
        ShowWindow(destination, SW_SHOWNA);
        ShowWindow(source, SW_SHOWNA);
        ShowWindow(other, SW_SHOWNA);
        OverviewHotkey hotkey;
        auto registered = hotkey.register_for(native(destination));
        if (registered.ready()) {
            OverviewHotkey contender;
            ok &= check(!contender.register_for(native(other)).ready(), "hotkey conflict not detected");
            MSG message{};
            PostMessageW(destination, WM_HOTKEY, 0x4344, MAKELPARAM(MOD_CONTROL | MOD_ALT, 'M'));
            bool delivered = false;
            while (PeekMessageW(&message, destination, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
                delivered |= hotkey.handle_message(message.message, message.wParam, message.lParam);
            }
            ok &= check(delivered, "hotkey event was not forwarded");
            hotkey.reset();
            ok &= check(contender.register_for(native(other)).ready(), "hotkey was not released");
        } else {
            // Another application may legitimately own the shortcut.
            std::cout << "Hotkey unavailable; reported error=" << registered.platform_error << '\n';
            ok &= check(registered.platform_error != 0, "hotkey failure lost error code");
        }
        WindowPreviews previews;
        auto results = previews.replace(native(destination), {
            {native(source), {10, 10, 200, 180}},
            {native(other), {220, 10, 410, 180}}
        });
        ok &= check(results.size() == 2 && results[0].ready() && results[1].ready(),
                    "two DWM registrations failed (requires interactive DWM desktop)");
        results = previews.replace(native(destination), {
            {native(source), {10, 10, 200, 180}}, {0, {220, 10, 410, 180}}
        });
        ok &= check(results[0].ready() && results[1].status == DesktopStatus::InvalidWindow,
                    "one invalid source broke valid preview");
        ShowWindow(source, SW_HIDE);
        previews.refresh();
        results = previews.replace(native(destination), {
            {native(source), {10, 10, 200, 180}}, {native(other), {220, 10, 410, 180}}
        });
        ok &= check(results[0].status == DesktopStatus::InvalidWindow && results[1].ready(),
                    "hidden source should fall back without affecting the other preview");
        ShowWindow(source, SW_SHOWNA);
        results = previews.replace(native(destination), {{native(source), {-1, 0, 50, 50}}});
        ok &= check(results[0].status == DesktopStatus::InvalidRectangle, "invalid slot accepted");
        results = previews.replace(native(source), {{native(source), {0, 0, 50, 50}}});
        ok &= check(results[0].status == DesktopStatus::InvalidWindow, "self thumbnail accepted");
        previews.replace(native(destination), {{native(source), {10, 10, 200, 180}}});
        DestroyWindow(source);
        source = nullptr;
        previews.refresh();
        previews.clear();
        previews.clear(); // Repeated cleanup must be safe.
    }
    if (source) DestroyWindow(source);
    if (destination) DestroyWindow(destination);
    if (other) DestroyWindow(other);
    UnregisterClassW(cls.lpszClassName, instance);
    return ok ? 0 : 1;
}
