// 平台命令层的真机测试。
//
// 为什么不用单元测试的思路写：置前和设置位置本质上是对真实窗口的操作，
// 用假 HWND 测等于什么都没测。这里启动一个真实的 crossdesk.exe 当靶子，
// 对它执行 place() 和 activate()，然后回读系统状态验证效果。
//
// 跑法：build\crossdesk_win32_commands_smoke.exe

#include "platform/windows/win32_window_commands.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int g_failures = 0;

void check(bool ok, const char* what, const std::string& detail = {}) {
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : " | ",
                detail.c_str());
    if (!ok) {
        ++g_failures;
    }
}

std::wstring exeDirectory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    const std::size_t slash = full.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : full.substr(0, slash);
}

struct WindowSearch { DWORD process_id; HWND found = nullptr; };

BOOL CALLBACK findCrossDesk(HWND hwnd, LPARAM param) {
    auto& search = *reinterpret_cast<WindowSearch*>(param);
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    if (owner != search.process_id) return TRUE;
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    if (std::wstring(title).find(L"CrossDesk") == std::wstring::npos) {
        return TRUE;
    }
    search.found = hwnd;
    return FALSE;
}

HWND waitForWindow(DWORD process_id, int timeoutMs) {
    const DWORD deadline = GetTickCount() + static_cast<DWORD>(timeoutMs);
    while (GetTickCount() < deadline) {
        WindowSearch search{process_id};
        EnumWindows(findCrossDesk, reinterpret_cast<LPARAM>(&search));
        if (search.found != nullptr) {
            return search.found;
        }
        Sleep(100);
    }
    return nullptr;
}

bool rectAlmostEqual(const RECT& rect, int x, int y, int w, int h, int tolerance = 2) {
    return std::abs(static_cast<int>(rect.left) - x) <= tolerance &&
           std::abs(static_cast<int>(rect.top) - y) <= tolerance &&
           std::abs(static_cast<int>(rect.right - rect.left) - w) <= tolerance &&
           std::abs(static_cast<int>(rect.bottom - rect.top) - h) <= tolerance;
}

}  // namespace

int main() {
    const std::wstring dir = exeDirectory();
    const std::wstring target = dir + L"\\crossdesk.exe";

    // 靶子：用真实程序当被测窗口，避免依赖记事本这类随系统版本变化的程序。
    std::wstring command = L"\"" + target + L"\" --fake --windowed";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(),
                       &startup, &process) == FALSE) {
        std::printf("FATAL 无法启动靶子程序：%s\n", std::string(target.begin(), target.end()).c_str());
        return 2;
    }

    // A title-matching window owned by this test must never become the target.
    const HWND decoy = CreateWindowExW(0, L"STATIC", L"CrossDesk decoy", WS_OVERLAPPEDWINDOW,
                                      30, 30, 300, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(decoy, SW_SHOWNA);
    WindowSearch wrong_process{process.dwProcessId};
    findCrossDesk(decoy, reinterpret_cast<LPARAM>(&wrong_process));
    check(decoy != nullptr && wrong_process.found == nullptr, "title match from another PID ignored");
    DestroyWindow(decoy);

    HWND hwnd = waitForWindow(process.dwProcessId, 8000);
    DWORD owner = 0;
    if (hwnd) GetWindowThreadProcessId(hwnd, &owner);
    if (owner != process.dwProcessId) hwnd = nullptr;
    if (hwnd == nullptr) {
        std::printf("FATAL 8 秒内没等到 CrossDesk 窗口\n");
        TerminateProcess(process.hProcess, 0);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 2;
    }
    check(true, "找到真实目标窗口");

    crossdesk::Win32WindowCommands commands;

    // ---- 设置位置 ----
    {
        std::wstring error;
        const bool ok = commands.place(reinterpret_cast<std::uintptr_t>(hwnd), 120, 140, 1000, 680,
                                       &error);
        check(ok, "place() 返回成功",
              ok ? std::string() : std::string(error.begin(), error.end()));

        RECT actual{};
        GetWindowRect(hwnd, &actual);
        check(rectAlmostEqual(actual, 120, 140, 1000, 680), "系统里窗口矩形确实变成了目标值",
              "left=" + std::to_string(actual.left) + " top=" + std::to_string(actual.top) +
                  " w=" + std::to_string(actual.right - actual.left) +
                  " h=" + std::to_string(actual.bottom - actual.top));
    }

    // ---- 置前 ----
    {
        std::wstring error;
        const bool ok = commands.activate(reinterpret_cast<std::uintptr_t>(hwnd), &error);
        const HWND foreground = GetForegroundWindow();
        check(ok, "activate() 返回成功",
              ok ? std::string() : std::string(error.begin(), error.end()));
        check(foreground == hwnd, "前台窗口确实变成了目标窗口");
    }

    // ---- 非法输入：已被关闭的窗口必须报失败，不能崩 ----
    {
        std::wstring error;
        const bool ok = commands.activate(static_cast<std::uintptr_t>(0xDEADBEEFu), &error);
        check(!ok, "对不存在的句柄返回失败而不是崩溃");
        check(!error.empty(), "失败时给出了可读原因");
    }

    TerminateProcess(process.hProcess, 0);
    WaitForSingleObject(process.hProcess, 3000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    std::printf("\n%s：%d 项失败\n", g_failures == 0 ? "全部通过" : "有失败", g_failures);
    return g_failures == 0 ? 0 : 1;
}
