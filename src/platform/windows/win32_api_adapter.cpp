#include "platform/windows/win32_api_adapter.h"

#include <windows.h>

#include <string>

namespace crossdesk {
namespace {

HWND to_hwnd(NativeWindowHandle window) {
    return reinterpret_cast<HWND>(window);
}

NativeWindowHandle to_native_handle(HWND window) {
    return reinterpret_cast<NativeWindowHandle>(window);
}

BOOL CALLBACK enum_windows_callback(HWND window, LPARAM parameter) {
    auto* callback = reinterpret_cast<const WindowApiAdapter::WindowCallback*>(
        parameter
    );
    if (callback == nullptr || !(*callback)(to_native_handle(window))) {
        return FALSE;
    }

    return TRUE;
}

}  // namespace

bool Win32ApiAdapter::enumerate_top_level_windows(
    const WindowCallback& callback
) const {
    if (!callback) {
        return false;
    }

    return EnumWindows(
        enum_windows_callback,
        reinterpret_cast<LPARAM>(&callback)
    ) != FALSE;
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

}  // namespace crossdesk
