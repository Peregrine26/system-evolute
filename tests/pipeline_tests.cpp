// ============================================================================
// Pipeline test: A's real WindowsWindowScanner feeding B's core logic.
//
// A Win32ApiAdapter is replaced by a fake that returns hand-written windows, so
// the whole A -> B path runs on any machine, including a headless one. This is
// the seam where the two modules actually meet, so it is tested separately from
// the pure core tests.
//
// Windows-only, because WindowsWindowScanner is Windows-only today.
// ============================================================================

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "core/core_types.h"
#include "core/window_filter.h"
#include "core/window_layout.h"
#include "core/window_selection.h"
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

// Minimal stand-in for Win32ApiAdapter. Each entry describes one window; an
// entry whose bounds_fail flag is set makes get_window_bounds() return false,
// which is what Win32 does when GetWindowRect fails (a window being destroyed,
// or one on another desktop).
struct FakeWindow {
    NativeWindowHandle handle = 0;
    std::uint32_t process_id = 0;
    std::wstring title;
    std::wstring process_name = L"app.exe";
    WindowBounds bounds{0, 0, 800, 600};
    bool visible = true;
    bool bounds_fail = false;
};

class FakeAdapter : public WindowApiAdapter {
public:
    explicit FakeAdapter(std::vector<FakeWindow> windows)
        : windows_(std::move(windows)) {}

    bool enumerate_top_level_windows(const WindowCallback& callback) const override {
        for (const FakeWindow& window : windows_) {
            if (!callback(window.handle)) {
                return false;
            }
        }
        return true;
    }

    std::wstring get_window_title(NativeWindowHandle window) const override {
        const FakeWindow* found = find(window);
        return found == nullptr ? std::wstring() : found->title;
    }

    std::uint32_t get_process_id(NativeWindowHandle window) const override {
        const FakeWindow* found = find(window);
        return found == nullptr ? 0 : found->process_id;
    }

    std::wstring get_process_name(std::uint32_t process_id) const override {
        for (const FakeWindow& window : windows_) {
            if (window.process_id == process_id) {
                return window.process_name;
            }
        }
        return L"<unknown>";
    }

    bool get_window_bounds(
        NativeWindowHandle window,
        WindowBounds& bounds
    ) const override {
        const FakeWindow* found = find(window);
        if (found == nullptr || found->bounds_fail) {
            return false;
        }
        bounds = found->bounds;
        return true;
    }

    bool is_window_visible(NativeWindowHandle window) const override {
        const FakeWindow* found = find(window);
        return found != nullptr && found->visible;
    }

private:
    const FakeWindow* find(NativeWindowHandle handle) const {
        for (const FakeWindow& window : windows_) {
            if (window.handle == handle) {
                return &window;
            }
        }
        return nullptr;
    }

    std::vector<FakeWindow> windows_;
};

// A desktop with four user windows plus one taskbar-like shell surface. The
// taskbar is expected to be excluded by A; here it is included on purpose to
// show what happens when it is not.
std::vector<FakeWindow> desktop() {
    return {
        FakeWindow{0x0000000000010A2Cu, 4000u, L"报告.docx - Word", L"WINWORD.EXE",
                   WindowBounds{0, 0, 1200, 900}, true, false},
        FakeWindow{0x0000000000020B40u, 4000u, L"课程主页 - 浏览器", L"chrome.exe",
                   WindowBounds{1200, 0, 1920, 900}, true, false},
        FakeWindow{0x0000000000030C11u, 4100u, L"", L"notepad.exe",
                   WindowBounds{100, 100, 700, 500}, true, false},
        FakeWindow{0x0000000000040D22u, 4200u, L"聊天 - 微信", L"WeChat.exe",
                   WindowBounds{-1920, 0, -1000, 800}, false, false},
        FakeWindow{0x0000000000050E33u, 4300u, L"任务栏", L"explorer.exe",
                   WindowBounds{0, 1040, 1920, 1080}, true, false},
    };
}

// ---------------------------------------------------------------------------
void scenario1_scanner_output() {
    section("Scenario 1: scanner output reaches the core unchanged");

    std::vector<FakeWindow> windows = desktop();
    const FakeAdapter api(windows);
    const WindowsWindowScanner scanner(api);
    const std::vector<WindowInfo> scanned = scanner.scan();

    CHECK(scanned.size() == windows.size());
    CHECK(scanned.size() == 5);

    // Fields the core keys on must survive the round trip.
    const WindowInfo& first = scanned[0];
    CHECK(first.native_handle == 0x0000000000010A2Cu);
    CHECK(first.visible);
    CHECK(!first.title.empty());
    CHECK(first.bounds.left == 0 && first.bounds.right == 1200);

    // The unavailable window keeps its record and its visible == false flag.
    CHECK(scanned[3].native_handle == 0x0000000000040D22u);
    CHECK(!scanned[3].visible);

    // Process data is informational only and never used as a key; the real
    // adapter fills placeholders such as "<access denied>" when a query fails,
    // which must not be a reason to drop the window.
    CHECK(scanned[3].process_name == L"WeChat.exe");
    CHECK(scanned[3].process_id == 4200u);
    std::cout << "    windows from scanner: " << scanned.size() << "\n";
}

void scenario2_filter_on_real_scan() {
    section("Scenario 2: filter applied to a real scan result");

    const FakeAdapter api(desktop());
    const WindowsWindowScanner scanner(api);

    const std::vector<WindowInfo> candidates = selectCandidates(scanner.scan());

    // Word, browser and the taskbar survive the two rules; the untitled notepad
    // and the hidden chat window do not.
    CHECK(candidates.size() == 3);
    std::cout << "    candidates: " << candidates.size() << "\n";
    std::cout << "    note: the taskbar window survives because filtering by\n"
                 "          title/visibility cannot recognise shell surfaces -\n"
                 "          excluding them is A's job before the handover\n";
}

void scenario3_select_then_layout() {
    section("Scenario 3: pick two candidates, then lay them out");

    const FakeAdapter api(desktop());
    const WindowsWindowScanner scanner(api);
    const std::vector<WindowInfo> scanned = scanner.scan();
    const std::vector<WindowInfo> candidates = selectCandidates(scanned);

    const ScanSnapshot snapshot{scanned, true};

    // Two windows from different monitors, which is exactly the awkward case:
    // the layout function may only receive one monitor's work area.
    const SelectionResult picked = validateSelection(
        snapshot, candidates[0].native_handle, candidates[1].native_handle);
    CHECK(picked.ok());
    std::cout << "    selection: " << picked.message << "\n";

    // The core does not choose the monitor; A or C passes one work area in.
    const WindowBounds primaryWorkArea{0, 0, 1920, 1040};
    const LayoutResult layout = computeLayout(primaryWorkArea, LayoutMode::Equal5050);
    CHECK(layout.ok());
    CHECK(layout.ok() && layout.right.right == primaryWorkArea.right);
    CHECK(layout.ok() &&
          boundsWidth(layout.left) + kWindowGapUnits + boundsWidth(layout.right) ==
              boundsWidth(primaryWorkArea));
    std::cout << "    layout 50:50 -> left width " << boundsWidth(layout.left)
              << ", right width " << boundsWidth(layout.right) << "\n";
}

void scenario4_missing_scan_signal() {
    section("Scenario 4: a window lost during the scan looks exactly like a closed one");

    std::vector<FakeWindow> windows = desktop();
    // This window still exists but its bounds query fails (destroyed in the
    // middle of the scan, or on another virtual desktop).
    const NativeWindowHandle failing = windows[1].handle;
    windows[1].bounds_fail = true;

    const FakeAdapter api(std::move(windows));
    const WindowsWindowScanner scanner(api);
    const std::vector<WindowInfo> scanned = scanner.scan();

    // WindowsWindowScanner::scan() skips it: the record never reaches the core.
    CHECK(scanned.size() == 4);
    bool found = false;
    for (const WindowInfo& window : scanned) {
        if (window.native_handle == failing) {
            found = true;
        }
    }
    CHECK(!found);
    std::cout << "    windows after one failed bounds query: " << scanned.size()
              << " (the failing one is gone, not marked)\n";

    // With no completeness signal, the core has to assume the scan was complete
    // and therefore reports "closed" for a window that is still on screen.
    const ScanSnapshot snapshot{scanned, true};
    const SelectionResult verdict = validateSelection(snapshot, 0x0000000000010A2Cu, failing);
    CHECK(verdict.error == SelectionError::WindowGone);
    std::cout << "    core verdict: " << verdict.message << "\n";
    std::cout << "    this is why ScanSnapshot::complete needs a real source in A's\n"
                 "    scanner: scan() currently cannot express \"I did not see all\"\n";
}

}  // namespace

int main() {
    enableUtf8Console();

    std::cout << "CrossDesk A -> B pipeline tests (fake adapter, no real desktop)\n";

    scenario1_scanner_output();
    scenario2_filter_on_real_scan();
    scenario3_select_then_layout();
    scenario4_missing_scan_signal();

    std::cout << "\n----------------------------------------\n";
    std::cout << "checks: " << g_checks << ", failed: " << g_failed << "\n";
    std::cout << (g_failed == 0 ? "all passed\n" : "failures listed above\n");
    return g_failed == 0 ? 0 : 1;
}
