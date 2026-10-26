// Hidden windows on this thread only: no focus, DWM, global keys or user targets.
#include "platform/windows/win32_window_commands.h"
#include <windows.h>

#include <iostream>

namespace {
bool close_on_move = false;
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (close_on_move && message == WM_WINDOWPOSCHANGED) {
        close_on_move = false;
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

int main() {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = procedure;
    cls.lpszClassName = L"CrossDeskCloseDuringMoveTest";
    if (!RegisterClassW(&cls)) return 2;
    const HWND window = CreateWindowW(cls.lpszClassName, L"Own hidden fixture",
        WS_OVERLAPPEDWINDOW, 0, 0, 320, 200, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window) { UnregisterClassW(cls.lpszClassName, cls.hInstance); return 2; }
    crossdesk::Win32WindowCommands commands;
    std::wstring error;
    bool ok = commands.place(reinterpret_cast<std::uintptr_t>(window), 10, 10, 400, 300, &error);
    error.clear();
    ok &= !commands.place(reinterpret_cast<std::uintptr_t>(window), 10, 10, 0, 300, &error)
          && !error.empty();
    error.clear();
    close_on_move = true;
    const bool moved = commands.place(reinterpret_cast<std::uintptr_t>(window), 20, 20, 410, 310, &error);
    ok &= !moved && !IsWindow(window) && !error.empty();
    if (IsWindow(window)) DestroyWindow(window);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    if (!ok) std::cerr << "move verification failed or a closed source was reported successful\n";
    return ok ? 0 : 1;
}
