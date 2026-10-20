#include "platform/windows/win32_api_adapter.h"

#include <windows.h>
#include <dwmapi.h>

#include <limits>
#include <initializer_list>
#include <cwchar>
#include <string>

namespace crossdesk {
namespace {

HWND to_hwnd(NativeWindowHandle window) {
    return reinterpret_cast<HWND>(window);
}

NativeWindowHandle to_native_handle(HWND window) {
    return reinterpret_cast<NativeWindowHandle>(window);
}

struct EnumerationContext {
    const WindowApiAdapter::WindowCallback* callback;
    bool stopped = false;
};

BOOL CALLBACK enum_windows_callback(HWND window, LPARAM parameter) {
    auto* context = reinterpret_cast<EnumerationContext*>(parameter);
    if (!(*context->callback)(to_native_handle(window))) {
        context->stopped = true;
        return FALSE;
    }

    return TRUE;
}

WindowOperationResult invalid_window() {
    return {WindowOperationStatus::InvalidWindow, ERROR_INVALID_WINDOW_HANDLE};
}

WindowOperationResult observed_visibility(HWND window, bool wanted) {
    if (IsWindow(window) == FALSE) {
        return invalid_window();
    }
    return {(IsWindowVisible(window) != FALSE) == wanted
                ? WindowOperationStatus::ReachedTarget
                : WindowOperationStatus::RequestedUnconfirmed,
            0};
}

}  // namespace

bool Win32ApiAdapter::get_primary_work_area(WindowBounds& bounds) const {
    RECT rect{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &rect, 0)) return false;
    bounds = {static_cast<int>(rect.left), static_cast<int>(rect.top),
              static_cast<int>(rect.right), static_cast<int>(rect.bottom)};
    return true;
}

bool Win32ApiAdapter::is_manageable_window(NativeWindowHandle window) const {
    const HWND hwnd = to_hwnd(window);
    if (!IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd ||
        hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) return false;
    wchar_t name[256]{};
    if (!GetClassNameW(hwnd, name, 256)) return false;
    // Surface classes, never explorer.exe / system app process names.
    for (const auto* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd",
                             L"Shell_SecondaryTrayWnd", L"DV2ControlHost"}) {
        if (std::wstring(name) == shell) return false;
    }
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED,
                                      &cloaked, sizeof(cloaked))) && cloaked) {
        return false;
    }
    // Start menu / shell overlay hosts are not application targets.
    const auto process = get_process_name(get_process_id(window));
    return _wcsicmp(process.c_str(), L"StartMenuExperienceHost.exe") != 0 &&
           _wcsicmp(process.c_str(), L"ShellExperienceHost.exe") != 0 &&
           _wcsicmp(process.c_str(), L"SearchHost.exe") != 0 &&
           _wcsicmp(process.c_str(), L"SearchApp.exe") != 0 &&
           _wcsicmp(process.c_str(), L"consent.exe") != 0 &&
           _wcsicmp(process.c_str(), L"LogonUI.exe") != 0;
}

bool Win32ApiAdapter::enumerate_top_level_windows(
    const WindowCallback& callback
) const {
    if (!callback) {
        return false;
    }

    EnumerationContext context{&callback};
    const BOOL completed = EnumWindows(
        enum_windows_callback,
        reinterpret_cast<LPARAM>(&context)
    );
    return completed != FALSE || context.stopped;
}

std::wstring Win32ApiAdapter::get_window_title(
    NativeWindowHandle window
) const {
    HWND hwnd = to_hwnd(window);
    const int title_length = GetWindowTextLengthW(hwnd);
    if (title_length <= 0) {
        return {};
    }

    std::wstring title(static_cast<std::size_t>(title_length) + 1, L'\0');
    const int copied = GetWindowTextW(
        hwnd,
        title.data(),
        static_cast<int>(title.size())
    );
    if (copied <= 0) {
        return {};
    }

    title.resize(static_cast<std::size_t>(copied));
    return title;
}

std::uint32_t Win32ApiAdapter::get_process_id(
    NativeWindowHandle window
) const {
    DWORD process_id = 0;
    GetWindowThreadProcessId(to_hwnd(window), &process_id);
    return process_id;
}

std::wstring Win32ApiAdapter::get_process_name(
    std::uint32_t process_id
) const {
    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        process_id
    );
    if (process == nullptr) {
        return L"<access denied>";
    }

    std::wstring path(260, L'\0');
    DWORD path_length = static_cast<DWORD>(path.size());

    while (QueryFullProcessImageNameW(
        process,
        0,
        path.data(),
        &path_length
    ) == FALSE) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            CloseHandle(process);
            return L"<unavailable>";
        }

        path.resize(path.size() * 2);
        path_length = static_cast<DWORD>(path.size());
    }

    CloseHandle(process);
    path.resize(path_length);

    const std::size_t last_separator = path.find_last_of(L"\\/");
    if (last_separator == std::wstring::npos) {
        return path;
    }

    return path.substr(last_separator + 1);
}

bool Win32ApiAdapter::get_window_bounds(
    NativeWindowHandle window,
    WindowBounds& bounds
) const {
    RECT rect{};
    if (GetWindowRect(to_hwnd(window), &rect) == FALSE) {
        return false;
    }

    bounds.left = rect.left;
    bounds.top = rect.top;
    bounds.right = rect.right;
    bounds.bottom = rect.bottom;
    return true;
}

bool Win32ApiAdapter::is_window_visible(
    NativeWindowHandle window
) const {
    return IsWindowVisible(to_hwnd(window)) != FALSE;
}

bool Win32ApiAdapter::is_window_minimized(
    NativeWindowHandle window
) const {
    return IsIconic(to_hwnd(window)) != FALSE;
}

WindowOperationResult Win32ApiAdapter::hide_window(
    NativeWindowHandle window
) const {
    const HWND hwnd = to_hwnd(window);
    if (IsWindow(hwnd) == FALSE) {
        return invalid_window();
    }
    if (IsWindowVisible(hwnd) == FALSE) {
        return {WindowOperationStatus::AlreadyInState, 0};
    }
    SetLastError(ERROR_SUCCESS);
    if (ShowWindowAsync(hwnd, SW_HIDE) == FALSE) {
        return {WindowOperationStatus::Rejected, GetLastError()};
    }
    return observed_visibility(hwnd, false);
}

WindowOperationResult Win32ApiAdapter::show_window_without_activation(
    NativeWindowHandle window
) const {
    const HWND hwnd = to_hwnd(window);
    if (IsWindow(hwnd) == FALSE) {
        return invalid_window();
    }
    if (IsWindowVisible(hwnd) != FALSE) {
        return {WindowOperationStatus::AlreadyInState, 0};
    }
    SetLastError(ERROR_SUCCESS);
    const int command = IsIconic(hwnd) != FALSE ? SW_SHOWMINNOACTIVE : SW_SHOWNA;
    if (ShowWindowAsync(hwnd, command) == FALSE) {
        return {WindowOperationStatus::Rejected, GetLastError()};
    }
    return observed_visibility(hwnd, true);
}

WindowOperationResult Win32ApiAdapter::set_window_bounds(
    NativeWindowHandle window,
    const WindowBounds& bounds
) const {
    const HWND hwnd = to_hwnd(window);
    if (IsWindow(hwnd) == FALSE) {
        return invalid_window();
    }
    const auto width = static_cast<long long>(bounds.right) - bounds.left;
    const auto height = static_cast<long long>(bounds.bottom) - bounds.top;
    if (width <= 0 || height <= 0 ||
        width > std::numeric_limits<int>::max() ||
        height > std::numeric_limits<int>::max()) {
        return {WindowOperationStatus::InvalidArgument, 0};
    }
    SetLastError(ERROR_SUCCESS);
    if (SetWindowPos(hwnd, nullptr, bounds.left, bounds.top,
                     static_cast<int>(width), static_cast<int>(height),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS) == FALSE) {
        return {WindowOperationStatus::Rejected, GetLastError()};
    }
    if (IsWindow(hwnd) == FALSE) {
        return invalid_window();
    }
    RECT actual{};
    if (GetWindowRect(hwnd, &actual) == FALSE) {
        return {WindowOperationStatus::RequestedUnconfirmed, 0};
    }
    const bool matches = actual.left == bounds.left &&
                         actual.top == bounds.top &&
                         actual.right == bounds.right &&
                         actual.bottom == bounds.bottom;
    return {matches ? WindowOperationStatus::ReachedTarget
                    : WindowOperationStatus::RequestedUnconfirmed, 0};
}

WindowOperationResult Win32ApiAdapter::activate_window(
    NativeWindowHandle window
) const {
    const HWND hwnd = to_hwnd(window);
    if (IsWindow(hwnd) == FALSE) {
        return invalid_window();
    }
    if (GetForegroundWindow() == hwnd && !IsIconic(hwnd)) {
        return {WindowOperationStatus::AlreadyInState, 0};
    }
    // Explicit locate is allowed to restore a minimized selection. Ordinary
    // show_window_without_activation keeps its original non-restoring contract.
    if (IsIconic(hwnd)) ShowWindowAsync(hwnd, SW_RESTORE);
    // Foreground permission is restricted by Windows. Its failure does not
    // provide a dependable GetLastError value.
    if (SetForegroundWindow(hwnd) == FALSE) {
        return {WindowOperationStatus::Rejected, 0};
    }
    if (IsWindow(hwnd) == FALSE) {
        return invalid_window();
    }
    return {GetForegroundWindow() == hwnd && !IsIconic(hwnd)
                ? WindowOperationStatus::ReachedTarget
                : WindowOperationStatus::RequestedUnconfirmed, 0};
}

}  // namespace crossdesk
