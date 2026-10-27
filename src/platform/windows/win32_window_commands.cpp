#include "win32_window_commands.h"

#include <windows.h>

#include <cstdlib>
#include <iterator>
#include <string>

namespace crossdesk {

namespace {

void setError(std::wstring* error, const std::wstring& text) {
    if (error != nullptr) {
        *error = text;
    }
}

std::wstring withSystemMessage(const wchar_t* what) {
    const DWORD code = GetLastError();
    wchar_t buffer[512] = {};
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, buffer,
                                        static_cast<DWORD>(std::size(buffer)), nullptr);

    std::wstring text(what);
    text += L"（错误码 ";
    text += std::to_wstring(code);
    text += L"）";
    if (length > 0) {
        std::wstring detail(buffer, length);
        while (!detail.empty() && (detail.back() == L'\r' || detail.back() == L'\n' ||
                                    detail.back() == L' ')) {
            detail.pop_back();
        }
        if (!detail.empty()) {
            text += L"：";
            text += detail;
        }
    }
    return text;
}

}  // namespace

bool Win32WindowCommands::activate(std::uintptr_t handle, std::wstring* error) {
    HWND hwnd = reinterpret_cast<HWND>(handle);
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        setError(error, L"窗口已不存在，可能刚刚被关闭");
        return false;
    }

    // Never join a foreign input queue: a stalled target must not stall us.
    // This bounded observation runs on a worker, never on the Qt event loop.
    ShowWindowAsync(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
    bool retriedAfterRestore = false;
    const ULONGLONG start = GetTickCount64();
    do {
        if (!IsWindow(hwnd)) {
            setError(error, L"窗口已在定位期间关闭");
            return false;
        }
        if (GetForegroundWindow() == hwnd && IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
            return true;
        }
        if (!retriedAfterRestore && IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
            // Restore is asynchronous; the initial request can precede it.
            SetForegroundWindow(hwnd);
            retriedAfterRestore = true;
        }
        Sleep(10);
    } while (GetTickCount64() - start < 1500);
    setError(error, L"未能确认置前完成（请求可能稍后执行，请核对后重试）");
    return false;
}

bool Win32WindowCommands::place(std::uintptr_t handle,
                                int left,
                                int top,
                                int width,
                                int height,
                                std::wstring* error) {
    HWND hwnd = reinterpret_cast<HWND>(handle);
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        setError(error, L"窗口已不存在，可能刚刚被关闭");
        return false;
    }
    if (width <= 0 || height <= 0) {
        setError(error, L"目标矩形无效（宽或高不是正数）");
        return false;
    }

    // 最小化状态下 SetWindowPos 改的是"还原后"的位置，但窗口仍然是最小化的，
    // 用户看不到效果。先还原再摆位。
    if (IsIconic(hwnd)) {
        ShowWindowAsync(hwnd, SW_RESTORE);
    }

    SetLastError(0);
    const BOOL moved = SetWindowPos(hwnd, nullptr, left, top, width, height,
                                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    if (moved == FALSE) {
        setError(error, withSystemMessage(L"调整窗口位置失败"));
        return false;
    }

    // An accepted asynchronous request is not success. Observe actual bounds
    // for a bounded time, including closure and restore failures.
    const ULONGLONG start = GetTickCount64();
    do {
        RECT actual{};
        if (!IsWindow(hwnd) || GetWindowRect(hwnd, &actual) == FALSE) {
            setError(error, L"无法核对移动后的窗口位置，窗口可能已关闭");
            return false;
        }
        const auto distance = [](long a, long long b) { return std::llabs(a - b); };
        if (!IsIconic(hwnd) &&
            distance(actual.left, left) <= 2 && distance(actual.top, top) <= 2 &&
            distance(actual.right - actual.left, width) <= 2 &&
            distance(actual.bottom - actual.top, height) <= 2) {
            return true;
        }
        Sleep(10);
    } while (GetTickCount64() - start < 1500);
    setError(error, L"未能确认排列完成（目标可能拒绝调整，请求也可能稍后执行，请核对后重试）");
    return false;
}

}  // namespace crossdesk
