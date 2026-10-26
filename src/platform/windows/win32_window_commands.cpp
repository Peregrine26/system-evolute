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

    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
    }

    if (SetForegroundWindow(hwnd) && GetForegroundWindow() == hwnd) {
        return true;
    }

    // 前台锁定（foreground lock）：Windows 只允许"当前在前台的进程"把前台权交出去。
    // 我们的窗口此刻在前台，本来有资格；但如果中间被别的进程抢过焦点就会失败。
    // 兜底做法是临时把自己的输入队列挂到目标线程上，借用它的前台权限。
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread =
        foreground != nullptr ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD targetThread = GetWindowThreadProcessId(hwnd, nullptr);

    bool attached = false;
    if (foregroundThread != 0 && targetThread != 0 && foregroundThread != targetThread) {
        attached = AttachThreadInput(foregroundThread, targetThread, TRUE) != FALSE;
    }
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    if (attached) {
        AttachThreadInput(foregroundThread, targetThread, FALSE);
    }

    if (GetForegroundWindow() == hwnd) {
        return true;
    }

    setError(error, L"Windows 拒绝了置前请求（目标窗口可能属于管理员权限的进程）");
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
        ShowWindow(hwnd, SW_RESTORE);
    }

    SetLastError(0);
    const BOOL moved = SetWindowPos(hwnd, nullptr, left, top, width, height,
                                    SWP_NOZORDER | SWP_NOACTIVATE);
    if (moved == FALSE) {
        setError(error, withSystemMessage(L"调整窗口位置失败"));
        return false;
    }

    // 有些窗口（提权窗口、部分 UWP/商店应用）会"调用成功但拒绝执行"。
    // 只看返回值会把失败当成成功，所以必须回读一次真实矩形。
    RECT actual{};
    if (GetWindowRect(hwnd, &actual) == FALSE) {
        setError(error, withSystemMessage(L"无法核对移动后的窗口位置，窗口可能已关闭"));
        return false;
    }
    const int dx = std::abs(static_cast<int>(actual.left) - left) +
                   std::abs(static_cast<int>(actual.top) - top);
    const int dw = std::abs(static_cast<int>(actual.right - actual.left) - width) +
                   std::abs(static_cast<int>(actual.bottom - actual.top) - height);
    if (dx > 2 || dw > 2) {
        setError(error, L"窗口拒绝调整到目标位置（可能是提权窗口或不允许改变大小）");
        return false;
    }

    return true;
}

}  // namespace crossdesk
