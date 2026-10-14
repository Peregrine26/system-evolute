#include "windows_window_scanner.h"

#include "platform/windows/win32_api_adapter.h"

#include <iostream>

namespace {

void print_window(const crossdesk::WindowInfo& window) {
    const int width = window.bounds.right - window.bounds.left;
    const int height = window.bounds.bottom - window.bounds.top;

    std::wcout << L"HWND: 0x"
               << std::hex << window.native_handle << std::dec
               << L"\n";
    std::wcout << L"  Title:   "
               << (window.title.empty() ? L"<empty>" : window.title)
               << L"\n";
    std::wcout << L"  Process: " << window.process_name
               << L" (PID " << window.process_id << L")\n";
    std::wcout << L"  Bounds:  (" << window.bounds.left << L", "
               << window.bounds.top
               << L") " << width << L"x" << height << L"\n";
    std::wcout << L"  Visible: " << (window.visible ? L"yes" : L"no")
               << L"\n\n";
}

}  // namespace

int wmain() {
    const crossdesk::Win32ApiAdapter api;
    const crossdesk::WindowsWindowScanner scanner(api);
    const std::vector<crossdesk::WindowInfo> windows = scanner.scan();

    std::wcout << L"CrossDesk Windows Window Scanner\n";
    std::wcout << L"Top-level windows found: " << windows.size() << L"\n\n";

    for (const crossdesk::WindowInfo& window : windows) {
        print_window(window);
    }

    return 0;
}
