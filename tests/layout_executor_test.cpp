#include "platform/layout_executor.h"

#include <iostream>
#include <vector>

using namespace crossdesk;
class FakeApi final : public WindowApiAdapter {
public:
    mutable std::vector<WindowInfo> windows;
    bool complete = true;
    bool move_now = true;
    mutable int calls = 0;
    FakeApi() {
        for (int id = 1; id <= 2; ++id) {
            WindowInfo info;
            info.native_handle = id;
            info.process_id = id + 100;
            info.title = L"test";
            info.visible = true;
            info.bounds = {0, 0, 100, 100};
            windows.push_back(info);
        }
    }
    const WindowInfo* find(NativeWindowHandle handle) const {
        for (const auto& w : windows) if (w.native_handle == handle) return &w;
        return nullptr;
    }
    bool enumerate_top_level_windows(const WindowCallback& callback) const override {
        for (const auto& w : windows) if (!callback(w.native_handle)) break;
        return complete;
    }
    std::wstring get_window_title(NativeWindowHandle w) const override { return find(w)->title; }
    std::uint32_t get_process_id(NativeWindowHandle w) const override { return find(w) ? find(w)->process_id : 0; }
    std::wstring get_process_name(std::uint32_t) const override { return L"explorer.exe"; }
    bool get_window_bounds(NativeWindowHandle w, WindowBounds& b) const override {
        if (!find(w)) return false;
        b = find(w)->bounds; return true;
    }
    bool is_window_visible(NativeWindowHandle w) const override { return find(w)->visible; }
    bool is_window_minimized(NativeWindowHandle) const override { return false; }
    WindowOperationResult hide_window(NativeWindowHandle) const override { return {}; }
    WindowOperationResult show_window_without_activation(NativeWindowHandle) const override { return {}; }
    WindowOperationResult activate_window(NativeWindowHandle) const override { return {}; }
    WindowOperationResult set_window_bounds(NativeWindowHandle w, const WindowBounds& b) const override {
        ++calls;
        if (w == 2) return {WindowOperationStatus::Rejected, 5};
        if (move_now) for (auto& info : windows) if (info.native_handle == w) info.bounds = b;
        return {WindowOperationStatus::RequestedUnconfirmed, 0};
    }
};
bool check(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}
int main() {
    FakeApi api;
    LayoutExecutor executor(api);
    std::array<WindowMoveTarget, 2> targets{{{1, 101, {100, 50, 594, 850}},
                                           {2, 102, {606, 50, 1101, 850}}}};
    bool ok = true;
    api.complete = false;
    auto execution = executor.submit(targets);
    ok &= check(!execution.submitted && api.calls == 0 &&
                execution.windows[0].observation == MoveObservation::ScanIncomplete,
                "partial scan must prevent all moves");
    api.complete = true;
    auto invalid = targets;
    invalid[1].window = 1;
    ok &= check(!executor.submit(invalid).submitted && api.calls == 0,
                "duplicate selection must prevent all moves");
    invalid = targets;
    invalid[1].process_id = 999;
    ok &= check(!executor.submit(invalid).submitted && api.calls == 0,
                "reused handle / changed process must prevent all moves");
    invalid = targets;
    invalid[1].bounds.right = invalid[1].bounds.left;
    ok &= check(!executor.submit(invalid).submitted && api.calls == 0,
                "invalid geometry must prevent all moves");
    api.move_now = false;
    execution = executor.submit(targets);
    ok &= check(execution.submitted && api.calls == 2 &&
                execution.windows[0].observation == MoveObservation::DifferentBounds &&
                execution.windows[1].request.status == WindowOperationStatus::Rejected,
                "accepted request must not imply arrival; retain per-window failures");
    api.windows[0].bounds = targets[0].bounds;
    executor.observe(execution);
    ok &= check(execution.windows[0].observation == MoveObservation::Reached,
                "delayed observation should see completed move");
    api.complete = false;
    executor.observe(execution);
    ok &= check(execution.windows[0].observation == MoveObservation::ScanIncomplete,
                "partial rescan must not report missing");
    api.complete = true;
    api.windows.erase(api.windows.begin());
    executor.observe(execution);
    ok &= check(execution.windows[0].observation == MoveObservation::Missing,
                "closed source should be reported per window");
    return ok ? 0 : 1;
}
