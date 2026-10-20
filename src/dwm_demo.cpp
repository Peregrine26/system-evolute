#include "platform/windows/desktop_services.h"

#include <windows.h>

#include <array>
#include <iostream>
#include <string>

namespace {
using namespace crossdesk;
NativeWindowHandle native(HWND window) { return reinterpret_cast<NativeWindowHandle>(window); }
struct Demo {
    HWND destination = nullptr;
    std::array<HWND, 2> sources{};
    WindowPreviews previews;
    OverviewHotkey hotkey;
    bool expanded = true;
    bool smoke = false;
    bool preview_ok = false;
    bool failed = false;
    unsigned ticks = 0;
    std::wstring status;

    void update() {
        if (!destination) return;
        if (!expanded || IsIconic(destination)) { previews.clear(); return; }
        RECT rect{};
        GetClientRect(destination, &rect);
        const int width = static_cast<int>(rect.right);
        const int height = static_cast<int>(rect.bottom);
        if (width < 100 || height < 120) { previews.clear(); return; }
        const int middle = width / 2;
        const auto results = previews.replace(native(destination), {
            {native(sources[0]), {12, 80, middle - 6, height - 12}},
            {native(sources[1]), {middle + 6, 80, width - 12, height - 12}}
        });
        preview_ok = results[0].ready() && results[1].ready();
        for (int i = 0; i < 2; ++i) {
            if (!results[i].ready()) {
                std::wcerr << L"Preview " << i + 1 << L" unavailable, status="
                           << static_cast<int>(results[i].status) << L" error="
                           << results[i].platform_error << L'\n';
            }
        }
        InvalidateRect(destination, nullptr, TRUE);
    }
};
LRESULT CALLBACK source_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_TIMER) { InvalidateRect(window, nullptr, FALSE); return 0; }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);
        const auto tick = GetTickCount64() / 100;
        HBRUSH brush = CreateSolidBrush(RGB(30 + tick % 160, 80, 170));
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        const auto text = L"Live source frame: " + std::to_wstring(tick);
        DrawTextW(dc, text.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
LRESULT CALLBACK destination_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* demo = reinterpret_cast<Demo*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        demo = static_cast<Demo*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(demo));
    }
    if (demo) {
        if (demo->hotkey.handle_message(message, wparam, lparam)) {
            demo->expanded = !demo->expanded;
            demo->update();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        }
        if (message == WM_SIZE) { demo->update(); return 0; }
        if (message == WM_TIMER) {
            demo->previews.refresh();
            if (demo->smoke && ++demo->ticks >= 30) {
                demo->failed = !demo->preview_ok;
                DestroyWindow(window);
            }
            return 0;
        }
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT rect{};
            GetClientRect(window, &rect);
            FillRect(dc, &rect, GetSysColorBrush(COLOR_WINDOW));
            rect.left = 12; rect.top = 12;
            const auto text = demo->status + L"\nResize to reposition. Close a source to test cleanup."
                L"\nMissing previews: source closed or DWM unavailable; inspect console.";
            DrawTextW(dc, text.c_str(), -1, &rect, DT_LEFT);
            EndPaint(window, &paint);
            return 0;
        }
        if (message == WM_DESTROY) {
            demo->previews.clear();
            demo->hotkey.reset();
            demo->destination = nullptr;
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

int main(int argc, char** argv) {
    Demo demo;
    demo.smoke = argc > 1 && std::string(argv[1]) == "--smoke";
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpfnWndProc = source_proc;
    cls.lpszClassName = L"CrossDeskLiveSource";
    if (!RegisterClassW(&cls)) return 1;
    cls.lpfnWndProc = destination_proc;
    cls.lpszClassName = L"CrossDeskPreviewDestination";
    if (!RegisterClassW(&cls)) return 1;
    for (int i = 0; i < 2; ++i) {
        demo.sources[i] = CreateWindowExW(0, L"CrossDeskLiveSource",
            i == 0 ? L"CrossDesk source A" : L"CrossDesk source B",
            WS_OVERLAPPEDWINDOW, 50 + i * 360, 50, 320, 220,
            nullptr, nullptr, instance, nullptr);
        if (!demo.sources[i]) { demo.failed = true; break; }
        ShowWindow(demo.sources[i], SW_SHOWNA);
        if (!SetTimer(demo.sources[i], 1, 100, nullptr)) demo.failed = true;
    }
    if (!demo.failed) {
        demo.destination = CreateWindowExW(0, L"CrossDeskPreviewDestination",
            L"CrossDesk DWM experiment", WS_OVERLAPPEDWINDOW, 50, 300, 760, 360,
            nullptr, nullptr, instance, &demo);
        if (!demo.destination) demo.failed = true;
    }
    if (demo.destination) {
        const auto result = demo.hotkey.register_for(native(demo.destination));
        demo.status = result.ready() ? L"Ctrl+Alt+M: toggle previews"
                                    : L"Ctrl+Alt+M unavailable; window still usable";
        std::cout << "Hotkey ready=" << result.ready() << " error=" << result.platform_error << '\n';
        ShowWindow(demo.destination, SW_SHOWNA);
        demo.update();
        if (!SetTimer(demo.destination, 1, 100, nullptr)) {
            demo.failed = true;
            DestroyWindow(demo.destination);
        }
        MSG message{};
        BOOL result_code;
        while ((result_code = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (result_code == -1) demo.failed = true;
    }
    demo.previews.clear();
    demo.hotkey.reset();
    if (IsWindow(demo.destination)) DestroyWindow(demo.destination);
    for (const auto source : demo.sources) if (IsWindow(source)) DestroyWindow(source);
    UnregisterClassW(L"CrossDeskLiveSource", instance);
    UnregisterClassW(L"CrossDeskPreviewDestination", instance);
    std::cout << "DWM registration smoke " << (demo.failed ? "FAILED" : "completed")
              << "; live visual updates require human observation.\n";
    return demo.failed ? 1 : 0;
}
