#include "window_catalog.h"
#include <iostream>
using namespace crossdesk;
int main() {
    std::vector<WindowInfo> windows;
    const auto add = [&](std::uintptr_t id, std::uint32_t pid, const wchar_t* title,
                         const wchar_t* process, bool visible = true) {
        WindowInfo info; info.native_handle = id; info.process_id = pid;
        info.title = title; info.process_name = process; info.visible = visible;
        windows.push_back(info);
    };
    // Same PID, unrelated/arbitrary titles: every self-owned window is absent.
    add(1, 42, L"CrossDesk", L"crossdesk.exe");
    add(2, 42, L"Documents", L"crossdesk.exe");
    add(3, 42, L"Preferences", L"crossdesk.exe");
    add(4, 42, L"Choose a folder", L"crossdesk.exe");
    add(5, 42, L"Window overview", L"crossdesk.exe");
    add(10, 100, L"File Explorer", L"explorer.exe");
    add(11, 101, L"Settings", L"SystemSettings.exe");
    add(12, 102, L"CrossDesk documentation", L"browser.exe");
    add(13, 103, L"Other application", L"ApplicationFrameHost.exe");
    add(14, 104, L"CrossDesk", L"editor.exe"); // Exact title collision stays.
    add(15, 105, L"", L"background.exe");
    add(16, 106, L"Hidden", L"hidden.exe", false);
    const auto actual = applicationWindows(windows, 42);
    if (actual.size() != 5) { std::cerr << "wrong candidate count\n"; return 1; }
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (actual[i].native_handle != 10 + i || actual[i].process_id == 42) {
            std::cerr << "PID exclusion or external application retention failed\n"; return 1;
        }
    return 0;
}
