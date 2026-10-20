#include "platform/windows/win32_api_adapter.h"

#include <windows.h>

#include <iostream>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool accepted(crossdesk::WindowOperationResult result) {
    return result.status == crossdesk::WindowOperationStatus::ReachedTarget ||
           result.status == crossdesk::WindowOperationStatus::RequestedUnconfirmed;
}

template <typename Predicate>
bool wait_until(Predicate predicate) {
    const ULONGLONG deadline = GetTickCount64() + 2000;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (predicate()) {
            return true;
        }
        Sleep(10);
    } while (GetTickCount64() < deadline);
    return predicate();
}

}  // namespace

int main() {
    const wchar_t* class_name = L"CrossDeskBackendSelfTest";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name;
    if (RegisterClassW(&window_class) == 0) {
        std::cerr << "RegisterClassW failed\n";
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, class_name, L"CrossDesk self-test",
                                WS_OVERLAPPEDWINDOW, 80, 80, 480, 320,
                                nullptr, nullptr, window_class.hInstance,
                                nullptr);
    if (hwnd == nullptr) {
        std::cerr << "CreateWindowExW failed\n";
        UnregisterClassW(class_name, window_class.hInstance);
        return 1;
    }

    const crossdesk::Win32ApiAdapter api;
    const auto handle = reinterpret_cast<crossdesk::NativeWindowHandle>(hwnd);
    ShowWindow(hwnd, SW_SHOWNA);

    bool passed = true;
    crossdesk::WindowBounds work{};
    passed &= check(api.get_primary_work_area(work) && work.right > work.left &&
                    work.bottom > work.top, "work area unavailable or invalid");
    passed &= check(api.is_manageable_window(handle), "ordinary window excluded");
    passed &= check(!api.is_manageable_window(
                        reinterpret_cast<crossdesk::NativeWindowHandle>(GetDesktopWindow())),
                    "desktop accepted as application");
    passed &= check(api.is_window_visible(handle), "test window not visible");
    passed &= check(accepted(api.hide_window(handle)), "hide request failed");
    passed &= check(wait_until([&] { return !api.is_window_visible(handle); }),
                    "hide not observed");
    passed &= check(api.hide_window(handle).status ==
                        crossdesk::WindowOperationStatus::AlreadyInState,
                    "already hidden window was not distinguished");
    passed &= check(accepted(api.show_window_without_activation(handle)),
                    "show failed");
    passed &= check(wait_until([&] { return api.is_window_visible(handle); }),
                    "show not observed");

    const crossdesk::WindowBounds bounds{120, 140, 620, 500};
    passed &= check(accepted(api.set_window_bounds(handle, bounds)),
                    "bounds change failed");
    crossdesk::WindowBounds actual{};
    passed &= check(wait_until([&] {
        return api.get_window_bounds(handle, actual) &&
               actual.left == bounds.left && actual.top == bounds.top &&
               actual.right == bounds.right && actual.bottom == bounds.bottom;
    }), "bounds do not match");

    ShowWindow(hwnd, SW_MINIMIZE);
    passed &= check(api.is_window_minimized(handle), "minimize not observed");
    passed &= check(accepted(api.hide_window(handle)),
                    "hide minimized window failed");
    passed &= check(wait_until([&] { return !api.is_window_visible(handle); }),
                    "minimized window was not hidden");
    passed &= check(accepted(api.show_window_without_activation(handle)),
                    "show minimized window failed");
    passed &= check(wait_until([&] { return api.is_window_visible(handle); }),
                    "minimized window was not shown");
    passed &= check(api.is_window_minimized(handle),
                    "show unexpectedly restored minimized window");

    DestroyWindow(hwnd);
    passed &= check(api.hide_window(handle).status ==
                        crossdesk::WindowOperationStatus::InvalidWindow,
                    "closed window was not rejected");
    UnregisterClassW(class_name, window_class.hInstance);
    return passed ? 0 : 1;
}
