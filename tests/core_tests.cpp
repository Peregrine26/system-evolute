// ============================================================================
// Core logic acceptance tests (B).
//
// Everything is built from hand-written WindowInfo values: no Win32, no Qt, no
// real desktop, no screen resolution. One command runs all of it, and every
// expected number below can be re-derived on paper during the demo.
// ============================================================================

#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/core_types.h"
#include "core/window_filter.h"
#include "core/window_layout.h"
#include "core/window_selection.h"
#include "platform/window_api_adapter.h"
#include "window_info.h"

// The core defines its own WindowId instead of including the adapter header.
// This is the guard that keeps the two in sync.
static_assert(
    std::is_same<crossdesk::WindowId, crossdesk::NativeWindowHandle>::value,
    "core WindowId must stay identical to the adapter's NativeWindowHandle"
);

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

WindowInfo makeWindow(
    WindowId handle,
    std::wstring title,
    bool visible,
    std::wstring process_name = L"app.exe"
) {
    WindowInfo window;
    window.native_handle = handle;
    window.process_id = 1000;
    window.process_name = std::move(process_name);
    window.title = std::move(title);
    window.visible = visible;
    window.bounds = WindowBounds{0, 0, 800, 600};
    return window;
}

// Prints a rectangle as "(left, top, right, bottom)" so the output can be
// compared character by character with the numbers in the task order.
std::string show(const WindowBounds& b) {
    return "(" + std::to_string(b.left) + ", " + std::to_string(b.top) + ", " +
           std::to_string(b.right) + ", " + std::to_string(b.bottom) + ")";
}

// ---------------------------------------------------------------------------
void scenario1_filter() {
    section("Scenario 1: normal / untitled / invisible - keep only the normal one");

    const std::vector<WindowInfo> scanned = {
        makeWindow(1, L"无标题 - 记事本", true),      // keep
        makeWindow(2, L"", true),                    // no title -> drop
        makeWindow(3, L"浏览器 - 课程主页", false),   // invisible -> drop
    };

    const std::vector<WindowInfo> candidates = selectCandidates(scanned);
    CHECK(candidates.size() == 1);
    CHECK(candidates.size() == 1 && candidates[0].native_handle == 1);
    std::cout << "    candidates: " << candidates.size() << "\n";
}

void scenario2_explorer_and_identity() {
    section("Scenario 2: File Explorer stays; same title and PID are two windows");

    const std::vector<WindowInfo> scanned = {
        makeWindow(10, L"此电脑", true, L"explorer.exe"),
        makeWindow(11, L"设置", true, L"SystemSettings.exe"),
        makeWindow(12, L"文档 - 记事本", true, L"notepad.exe"),
        makeWindow(13, L"文档 - 记事本", true, L"notepad.exe"),
    };

    const std::vector<WindowInfo> candidates = selectCandidates(scanned);
    CHECK(candidates.size() == 4);  // no process-name filter, no title de-dup
    std::cout << "    candidates: " << candidates.size()
              << " (Explorer plus two identical notepads)\n";

    // A blank title is still blank in other forms: spaces, tab, full-width
    // space, no-break space.
    const std::vector<WindowInfo> blanks = {
        makeWindow(20, L"    ", true),
        makeWindow(21, L"\u3000", true),
        makeWindow(22, L"\t\r\n", true),
        makeWindow(23, L"\u00A0", true),
    };
    CHECK(selectCandidates(blanks).empty());

    // Real HWND values are large and sparse; nothing may assume small ids.
    const std::vector<WindowInfo> realHandles = {
        makeWindow(0x00000000001A0C32u, L"窗口 A", true),
        makeWindow(0x0000000000FF1044u, L"窗口 B", true),
    };
    const std::vector<WindowInfo> real = selectCandidates(realHandles);
    CHECK(real.size() == 2);
    CHECK(real.size() == 2 &&
          validateSelection(ScanSnapshot{realHandles, true},
                            0x00000000001A0C32u,
                            0x0000000000FF1044u).ok());

    // A minimized window keeps WS_VISIBLE, so it must stay selectable: the user
    // needs it in the list to find it again. `minimized` is for the UI badge.
    WindowInfo minimized = makeWindow(30, L"音乐播放器", true);
    minimized.minimized = true;
    const std::vector<WindowInfo> withMinimized = selectCandidates({minimized});
    CHECK(withMinimized.size() == 1);
    CHECK(withMinimized.size() == 1 && withMinimized[0].minimized);
}

// ---------------------------------------------------------------------------
void scenario3_selection() {
    section("Scenario 3: same window, closed window, incomplete scan, hidden window");

    ScanSnapshot scan;
    scan.windows = {
        makeWindow(1, L"无标题 - 记事本", true),
        makeWindow(2, L"浏览器", true),
        makeWindow(3, L"已被隐藏的窗口", false),
    };
    scan.complete = true;

    const SelectionResult same = validateSelection(scan, 1, 1);
    CHECK(same.error == SelectionError::SameWindow);
    std::cout << "    same window: " << same.message << "\n";

    const SelectionResult gone = validateSelection(scan, 1, 999);
    CHECK(gone.error == SelectionError::WindowGone);
    std::cout << "    closed window (complete scan): " << gone.message << "\n";

    const SelectionResult hidden = validateSelection(scan, 1, 3);
    CHECK(hidden.error == SelectionError::NotCandidate);
    std::cout << "    hidden window: " << hidden.message << "\n";

    const SelectionResult good = validateSelection(scan, 1, 2);
    CHECK(good.ok());
    CHECK(good.ok() && good.first.native_handle == 1 &&
          good.second.native_handle == 2);
    std::cout << "    valid pick: " << good.message << "\n";

    // The important one: an incomplete scan must not report "closed".
    ScanSnapshot partial = scan;
    partial.complete = false;
    partial.windows = {makeWindow(1, L"无标题 - 记事本", true)};

    const SelectionResult incomplete = validateSelection(partial, 1, 2);
    CHECK(incomplete.error == SelectionError::ScanIncomplete);
    CHECK(incomplete.error != SelectionError::WindowGone);
    std::cout << "    missing window while scan incomplete: " << incomplete.message
              << "\n";
}

// ---------------------------------------------------------------------------
void checkGeometry(const LayoutResult& r, const WindowBounds& workArea) {
    CHECK(r.ok());
    if (!r.ok()) return;

    // No overlap: left right edge plus the gap lands exactly on the right left edge.
    CHECK(r.left.right + kWindowGapUnits == r.right.left);
    CHECK(r.left.right <= r.right.left);

    // No overflow: right window hugs the work area, both share top and bottom.
    CHECK(r.right.right == workArea.right);
    CHECK(r.left.left == workArea.left);
    CHECK(r.left.top == workArea.top && r.left.bottom == workArea.bottom);
    CHECK(r.right.top == workArea.top && r.right.bottom == workArea.bottom);

    // Both widths are positive.
    CHECK(boundsWidth(r.left) > 0);
    CHECK(boundsWidth(r.right) > 0);

    // Widths plus the gap equal the work area width: nothing lost, nothing double counted.
    CHECK(boundsWidth(r.left) + kWindowGapUnits + boundsWidth(r.right) ==
          boundsWidth(workArea));
}

void scenario4_layout_acceptance() {
    section("Scenario 4: acceptance numbers - work area (100, 50, 1101, 850)");

    const WindowBounds area{100, 50, 1101, 850};
    std::cout << "    width " << boundsWidth(area) << ", minus gap " << kWindowGapUnits
              << " leaves " << boundsWidth(area) - kWindowGapUnits << "\n";

    const LayoutResult half = computeLayout(area, LayoutMode::Equal5050);
    std::cout << "    50:50 left " << show(half.left) << "  right " << show(half.right)
              << "\n";
    CHECK(sameBounds(half.left, WindowBounds{100, 50, 594, 850}));
    CHECK(sameBounds(half.right, WindowBounds{606, 50, 1101, 850}));
    checkGeometry(half, area);

    const LayoutResult primary = computeLayout(area, LayoutMode::Primary6040);
    std::cout << "    60:40 left " << show(primary.left) << "  right "
              << show(primary.right) << "\n";
    CHECK(sameBounds(primary.left, WindowBounds{100, 50, 693, 850}));
    CHECK(sameBounds(primary.right, WindowBounds{705, 50, 1101, 850}));
    checkGeometry(primary, area);
}

void scenario5_layout_edges() {
    section("Scenario 5: empty, too narrow, just enough, negative origin, odd width");

    CHECK(computeLayout(WindowBounds{100, 50, 100, 850}, LayoutMode::Equal5050).error ==
          LayoutError::InvalidWorkArea);
    CHECK(computeLayout(WindowBounds{100, 50, 1101, 50}, LayoutMode::Equal5050).error ==
          LayoutError::InvalidWorkArea);
    CHECK(computeLayout(WindowBounds{100, 50, 90, 850}, LayoutMode::Equal5050).error ==
          LayoutError::InvalidWorkArea);
    CHECK(computeLayout(WindowBounds{0, 0, 100, 100}, LayoutMode::Equal5050, -1).error ==
          LayoutError::InvalidWorkArea);

    // 13 units: only 1 left after the gap, not a two-window layout.
    const LayoutResult tooNarrow =
        computeLayout(WindowBounds{0, 0, 13, 100}, LayoutMode::Equal5050);
    CHECK(tooNarrow.error == LayoutError::TooNarrow);
    std::cout << "    width 13: " << tooNarrow.message << "\n";

    // 14 units: exactly two 1-unit windows.
    const LayoutResult exact =
        computeLayout(WindowBounds{0, 0, 14, 100}, LayoutMode::Equal5050);
    CHECK(exact.ok());
    CHECK(exact.ok() && boundsWidth(exact.left) == 1 && boundsWidth(exact.right) == 1);
    std::cout << "    width 14: left " << show(exact.left) << "  right "
              << show(exact.right) << "\n";

    // Odd available width: the remainder goes to the right window, never lost.
    const LayoutResult odd =
        computeLayout(WindowBounds{0, 0, 15, 100}, LayoutMode::Equal5050);
    CHECK(odd.ok() && boundsWidth(odd.left) == 1 && boundsWidth(odd.right) == 2);

    // Negative origin: a second monitor left of the primary one.
    const WindowBounds second{-1920, 0, 0, 1080};
    const LayoutResult leftScreen = computeLayout(second, LayoutMode::Equal5050);
    std::cout << "    negative origin: left " << show(leftScreen.left) << "  right "
              << show(leftScreen.right) << "\n";
    CHECK(sameBounds(leftScreen.left, WindowBounds{-1920, 0, -966, 1080}));
    CHECK(sameBounds(leftScreen.right, WindowBounds{-954, 0, 0, 1080}));
    checkGeometry(leftScreen, second);
}

void scenario6_same_result_for_mouse_and_hotkey() {
    section("Scenario 6: mouse and hotkey must produce identical results");

    const WindowBounds area{100, 50, 1101, 850};

    const LayoutResult first = computeLayout(area, LayoutMode::Equal5050);
    (void)computeLayout(area, LayoutMode::Primary6040);  // switch mode in between
    const LayoutResult again = computeLayout(area, LayoutMode::Equal5050);

    CHECK(sameBounds(first.left, again.left));
    CHECK(sameBounds(first.right, again.right));

    // Validation keeps no state either.
    ScanSnapshot scan;
    scan.windows = {makeWindow(1, L"A", true), makeWindow(2, L"B", true)};
    const SelectionResult s1 = validateSelection(scan, 1, 2);
    const SelectionResult s2 = validateSelection(scan, 1, 2);
    CHECK(s1.ok() && s2.ok());
    CHECK(s1.first.native_handle == s2.first.native_handle &&
          s1.second.native_handle == s2.second.native_handle);
    std::cout << "    one pure function per decision, no keyboard state\n";
}

}  // namespace

int main() {
    enableUtf8Console();

    std::cout << "CrossDesk core logic acceptance tests (C++17 only, no Win32/Qt/desktop)\n";

    scenario1_filter();
    scenario2_explorer_and_identity();
    scenario3_selection();
    scenario4_layout_acceptance();
    scenario5_layout_edges();
    scenario6_same_result_for_mouse_and_hotkey();

    std::cout << "\n----------------------------------------\n";
    std::cout << "checks: " << g_checks << ", failed: " << g_failed << "\n";
    std::cout << (g_failed == 0 ? "all passed\n" : "failures listed above\n");
    return g_failed == 0 ? 0 : 1;
}
