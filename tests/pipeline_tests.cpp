// ============================================================================
// A -> B pipeline tests.
//
// Drives A's real WindowsWindowScanner and LayoutExecutor through a fake
// WindowApiAdapter, so the whole hand-over runs on any machine, including a
// headless one:
//
//   fake adapter -> WindowsWindowScanner -> B filter -> B validate
//                -> B layout -> LayoutExecutor -> fake adapter (bounds move)
//
// Windows-only for now, because the scanner and executor are compiled only
// there. No real window is ever created.
// ============================================================================

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "core/core_types.h"
#include "core/window_filter.h"
#include "core/window_layout.h"
#include "core/window_selection.h"
#include "platform/layout_executor.h"
#include "platform/window_api_adapter.h"
#include "window_info.h"
#include "windows_window_scanner.h"

using namespace crossdesk;

namespace {

int g_checks = 0;
int g_failed = 0;

void check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::cout << "    [FAIL] " << what << "  (line " << line << ")\n";
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void section(const char* name) { std::cout << "\n=== " << name << " ===\n"; }

void enableUtf8Console() {
#if defined(_WIN32)
    std::system("chcp 65001 > nul");
#endif
}

// One described window. The flags model the platform situations the core has to
// survive: a shell surface, a hidden window, a minimized one, a failing query,
// a refused move.
struct FakeWindow {
    NativeWindowHandle handle = 0;
    std::uint32_t process_id = 0;
    std::wstring title;
    std::wstring process_name = L"app.exe";
    WindowBounds bounds{0, 0, 800, 600};
    bool visible = true;
    bool minimized = false;
    bool shell = false;
    bool bounds_fail = false;
    bool move_refused = false;
};

class FakeAdapter : public WindowApiAdapter {
public:
    explicit FakeAdapter(std::vector<FakeWindow> windows, WindowBounds work_area)
        : windows_(std::move(windows)), work_area_(work_area) {}

    // Enumeration stops early, which is how WindowsWindowScanner learns that a
    // scan was not complete.
    void abort_after(int count) { abort_after_ = count; }

    const FakeWindow* window(NativeWindowHandle handle) const {
        for (const FakeWindow& window : windows_) {
            if (window.handle == handle) {
                return &window;
            }
        }
        return nullptr;
    }

    bool get_primary_work_area(WindowBounds& bounds) const override {
        bounds = work_area_;
        return true;
    }

    // A excludes shell surfaces here; the scanner therefore never hands them to
    // the core.
    bool is_manageable_window(NativeWindowHandle window) const override {
        const FakeWindow* found = this->window(window);
        return found != nullptr && !found->shell;
    }

    bool enumerate_top_level_windows(const WindowCallback& callback) const override {
        int seen = 0;
        for (const FakeWindow& window : windows_) {
            if (abort_after_ >= 0 && seen >= abort_after_) {
                return false;
            }
            ++seen;
            if (!callback(window.handle)) {
                return false;
            }
        }
        return true;
    }

    std::wstring get_window_title(NativeWindowHandle window) const override {
        const FakeWindow* found = this->window(window);
        return found == nullptr ? std::wstring() : found->title;
    }

    std::uint32_t get_process_id(NativeWindowHandle window) const override {
        const FakeWindow* found = this->window(window);
        return found == nullptr ? 0 : found->process_id;
    }

    std::wstring get_process_name(std::uint32_t process_id) const override {
        for (const FakeWindow& window : windows_) {
            if (window.process_id == process_id) {
                return window.process_name;
            }
        }
        return L"<access denied>";
    }

    bool get_window_bounds(
        NativeWindowHandle window,
        WindowBounds& bounds
    ) const override {
        const FakeWindow* found = this->window(window);
        if (found == nullptr || found->bounds_fail) {
            return false;
        }
        bounds = found->bounds;
        return true;
    }

    bool is_window_visible(NativeWindowHandle window) const override {
        const FakeWindow* found = this->window(window);
        return found != nullptr && found->visible;
    }

    bool is_window_minimized(NativeWindowHandle window) const override {
        const FakeWindow* found = this->window(window);
        return found != nullptr && found->minimized;
    }

    WindowOperationResult hide_window(NativeWindowHandle window) const override {
        FakeWindow* found = mutable_window(window);
        if (found == nullptr) {
            return {WindowOperationStatus::InvalidWindow, 0};
        }
        found->visible = false;
        return {WindowOperationStatus::ReachedTarget, 0};
    }

    WindowOperationResult show_window_without_activation(
        NativeWindowHandle window
    ) const override {
        FakeWindow* found = mutable_window(window);
        if (found == nullptr) {
            return {WindowOperationStatus::InvalidWindow, 0};
        }
        found->visible = true;
        return {WindowOperationStatus::ReachedTarget, 0};
    }

    WindowOperationResult set_window_bounds(
        NativeWindowHandle window,
        const WindowBounds& bounds
    ) const override {
        FakeWindow* found = mutable_window(window);
        if (found == nullptr) {
            return {WindowOperationStatus::InvalidWindow, 0};
        }
        if (found->move_refused) {
            return {WindowOperationStatus::Rejected, 5};  // ERROR_ACCESS_DENIED
        }
        if (sameBounds(found->bounds, bounds)) {
            return {WindowOperationStatus::AlreadyInState, 0};
        }
        found->bounds = bounds;
        return {WindowOperationStatus::ReachedTarget, 0};
    }

    WindowOperationResult activate_window(NativeWindowHandle window) const override {
        return window == 0
                   ? WindowOperationResult{WindowOperationStatus::InvalidWindow, 0}
                   : WindowOperationResult{WindowOperationStatus::AlreadyInState, 0};
    }

private:
    FakeWindow* mutable_window(NativeWindowHandle handle) const {
        for (const FakeWindow& window : windows_) {
            if (window.handle == handle) {
                return const_cast<FakeWindow*>(&window);
            }
        }
        return nullptr;
    }

    mutable std::vector<FakeWindow> windows_;
    WindowBounds work_area_;
    int abort_after_ = -1;
};

// A plausible desktop: two normal applications, one untitled window, one hidden
// chat window, one minimized player, and the taskbar that A must exclude.
std::vector<FakeWindow> desktop() {
    return {
        FakeWindow{0x0000000000010A2Cu, 4000u, L"报告.docx - Word", L"WINWORD.EXE",
                   WindowBounds{0, 0, 1200, 900}, true, false, false, false, false},
        FakeWindow{0x0000000000020B40u, 4001u, L"课程主页 - 浏览器", L"chrome.exe",
                   WindowBounds{1200, 0, 1920, 900}, true, false, false, false, false},
        FakeWindow{0x0000000000030C11u, 4002u, L"", L"notepad.exe",
                   WindowBounds{100, 100, 700, 500}, true, false, false, false, false},
        FakeWindow{0x0000000000040D22u, 4003u, L"聊天 - 微信", L"WeChat.exe",
                   WindowBounds{-1920, 0, -1000, 800}, false, false, false, false, false},
        FakeWindow{0x0000000000050E33u, 4004u, L"音乐播放器", L"music.exe",
                   WindowBounds{300, 300, 900, 700}, true, true, false, false, false},
        FakeWindow{0x0000000000060F44u, 4100u, L"任务栏", L"explorer.exe",
                   WindowBounds{0, 1040, 1920, 1080}, true, false, true, false, false},
    };
}

const WindowBounds kWorkArea{0, 0, 1920, 1040};

// ---------------------------------------------------------------------------
void scenario1_scanner_output() {
    section("Scenario 1: scan_with_status carries completeness, exclusion and minimized");

    const FakeAdapter api(desktop(), kWorkArea);
    const WindowsWindowScanner scanner(api);
    const WindowScanResult scan = scanner.scan_with_status();

    CHECK(scan.complete);             // enumeration ran to the end
    CHECK(scan.windows.size() == 5);  // 6 described, taskbar excluded by A
    CHECK(scan.windows[0].native_handle == 0x0000000000010A2Cu);
    CHECK(scan.windows[0].bounds.right == 1200);

    bool taskbar_present = false;
    bool minimized_present = false;
    for (const WindowInfo& window : scan.windows) {
        if (window.title == L"任务栏") taskbar_present = true;
        if (window.title == L"音乐播放器") {
            minimized_present = window.minimized && window.visible;
        }
    }
    CHECK(!taskbar_present);
    CHECK(minimized_present);
    std::cout << "    windows after A's exclusion: " << scan.windows.size()
              << ", complete: yes\n";
}

void scenario2_filter_on_real_scan() {
    section("Scenario 2: filter applied to a real scan result");

    const FakeAdapter api(desktop(), kWorkArea);
    const WindowsWindowScanner scanner(api);

    const std::vector<WindowInfo> candidates = selectCandidates(scanner.scan());

    // Word, browser and the minimized player survive; the untitled notepad and
    // the hidden chat window do not.
    CHECK(candidates.size() == 3);
    bool kept_minimized = false;
    for (const WindowInfo& window : candidates) {
        if (window.title == L"音乐播放器") kept_minimized = true;
    }
    CHECK(kept_minimized);  // minimized stays selectable on purpose
    std::cout << "    candidates: " << candidates.size()
              << " (minimized kept; hidden and untitled dropped)\n";
}

void scenario3_validation_feeds_executor() {
    section("Scenario 3: B rectangles handed to A's LayoutExecutor really move windows");

    FakeAdapter api(desktop(), kWorkArea);
    const WindowsWindowScanner scanner(api);
    const WindowScanResult scan = scanner.scan_with_status();
    const std::vector<WindowInfo> candidates = selectCandidates(scan.windows);

    const ScanSnapshot snapshot{scan.windows, scan.complete};
    const SelectionResult picked = validateSelection(
        snapshot, candidates[0].native_handle, candidates[1].native_handle);
    CHECK(picked.ok());
    std::cout << "    selection: " << picked.message << "\n";

    // A supplies the work area, B computes the rectangles.
    WindowBounds work_area;
    CHECK(api.get_primary_work_area(work_area));
    const LayoutResult layout = computeLayout(work_area, LayoutMode::Equal5050);
    CHECK(layout.ok());
    std::cout << "    B layout 50:50 -> left width " << boundsWidth(layout.left)
              << ", right width " << boundsWidth(layout.right) << "\n";

    // The glue A/C need: one WindowMoveTarget per window, carrying process_id so
    // the executor can catch a recycled handle before moving anything.
    const std::array<WindowMoveTarget, 2> targets{{
        WindowMoveTarget{picked.first.native_handle, picked.first.process_id, layout.left},
        WindowMoveTarget{picked.second.native_handle, picked.second.process_id, layout.right},
    }};

    const LayoutExecutor executor(api);
    const LayoutExecution execution = executor.submit(targets);

    CHECK(execution.submitted);
    CHECK(execution.windows[0].observation == MoveObservation::Reached);
    CHECK(execution.windows[1].observation == MoveObservation::Reached);
    CHECK(execution.windows[0].request.reached_target());
    CHECK(execution.windows[1].request.reached_target());

    // The fake really was moved, and to exactly B's numbers.
    const FakeWindow* moved = api.window(picked.first.native_handle);
    CHECK(moved != nullptr && sameBounds(moved->bounds, layout.left));
    CHECK(sameBounds(execution.windows[0].actual, layout.left));
    std::cout << "    executor observed: reached, left window at ("
              << execution.windows[0].actual.left << ", "
              << execution.windows[0].actual.top << ", "
              << execution.windows[0].actual.right << ", "
              << execution.windows[0].actual.bottom << ")\n";
}

void scenario4_refused_move_is_reported() {
    section("Scenario 4: a refused move keeps the old geometry and says so");

    std::vector<FakeWindow> windows = desktop();
    windows[1].move_refused = true;  // the browser refuses to resize

    FakeAdapter api(std::move(windows), kWorkArea);
    const WindowsWindowScanner scanner(api);
    const WindowScanResult scan = scanner.scan_with_status();
    const std::vector<WindowInfo> candidates = selectCandidates(scan.windows);
    const LayoutResult layout = computeLayout(kWorkArea, LayoutMode::Equal5050);

    const std::array<WindowMoveTarget, 2> targets{{
        WindowMoveTarget{candidates[0].native_handle, candidates[0].process_id, layout.left},
        WindowMoveTarget{candidates[1].native_handle, candidates[1].process_id, layout.right},
    }};

    const LayoutExecutor executor(api);
    const LayoutExecution execution = executor.submit(targets);

    CHECK(execution.submitted);
    CHECK(execution.windows[0].observation == MoveObservation::Reached);
    CHECK(execution.windows[1].request.status == WindowOperationStatus::Rejected);
    CHECK(!execution.windows[1].request.reached_target());
    CHECK(execution.windows[1].observation == MoveObservation::DifferentBounds);
    CHECK(sameBounds(execution.windows[1].actual, WindowBounds{1200, 0, 1920, 900}));
    std::cout << "    refused window kept (" << execution.windows[1].actual.left << ", "
              << execution.windows[1].actual.top << ", "
              << execution.windows[1].actual.right << ", "
              << execution.windows[1].actual.bottom << ")\n";
}

void scenario5_incomplete_scan() {
    section("Scenario 5: an aborted enumeration is incomplete, not closed");

    FakeAdapter api(desktop(), kWorkArea);
    api.abort_after(2);  // enumeration stops after two windows

    const WindowsWindowScanner scanner(api);
    const WindowScanResult scan = scanner.scan_with_status();
    CHECK(!scan.complete);
    CHECK(scan.windows.size() == 2);
    std::cout << "    windows seen before abort: " << scan.windows.size()
              << ", complete: no\n";

    const ScanSnapshot snapshot{scan.windows, scan.complete};
    const SelectionResult verdict =
        validateSelection(snapshot, 0x0000000000010A2Cu, 0x0000000000050E33u);
    CHECK(verdict.error == SelectionError::ScanIncomplete);
    CHECK(verdict.error != SelectionError::WindowGone);
    std::cout << "    core verdict: " << verdict.message << "\n";
}

void scenario6_failed_query_still_silent() {
    section("Scenario 6: a window lost to a failed query still looks closed");

    std::vector<FakeWindow> windows = desktop();
    const NativeWindowHandle failing = windows[1].handle;  // browser
    windows[1].bounds_fail = true;

    FakeAdapter api(std::move(windows), kWorkArea);
    const WindowsWindowScanner scanner(api);
    const WindowScanResult scan = scanner.scan_with_status();

    // EnumWindows itself succeeded, so `complete` stays true even though one
    // window is missing from the list.
    CHECK(scan.complete);
    CHECK(scan.windows.size() == 4);

    const ScanSnapshot snapshot{scan.windows, scan.complete};
    const SelectionResult verdict =
        validateSelection(snapshot, 0x0000000000010A2Cu, failing);
    CHECK(verdict.error == SelectionError::WindowGone);
    std::cout << "    core verdict: " << verdict.message << "\n";
    std::cout << "    open issue: `complete` only tracks the enumeration result, so a\n"
                 "    per-window query failure still turns a live window into 'closed'\n";
}

}  // namespace

int main() {
    enableUtf8Console();

    std::cout << "CrossDesk A -> B pipeline tests (fake adapter, no real desktop)\n";

    scenario1_scanner_output();
    scenario2_filter_on_real_scan();
    scenario3_validation_feeds_executor();
    scenario4_refused_move_is_reported();
    scenario5_incomplete_scan();
    scenario6_failed_query_still_silent();

    std::cout << "\n----------------------------------------\n";
    std::cout << "checks: " << g_checks << ", failed: " << g_failed << "\n";
    std::cout << (g_failed == 0 ? "all passed\n" : "failures listed above\n");
    return g_failed == 0 ? 0 : 1;
}
