#include "platform/layout_executor.h"

#include <algorithm>
#include <limits>

namespace crossdesk {
namespace {
const WindowInfo* find(const WindowScanResult& scan, const WindowMoveTarget& target) {
    const auto it = std::find_if(scan.windows.begin(), scan.windows.end(),
        [&](const auto& info) {
            return info.native_handle == target.window &&
                   info.process_id == target.process_id && info.visible &&
                   !info.title.empty();
        });
    return it == scan.windows.end() ? nullptr : &*it;
}
bool equal(const WindowBounds& a, const WindowBounds& b) {
    return a.left == b.left && a.top == b.top &&
           a.right == b.right && a.bottom == b.bottom;
}
bool valid(const WindowBounds& bounds) {
    const auto width = static_cast<long long>(bounds.right) - bounds.left;
    const auto height = static_cast<long long>(bounds.bottom) - bounds.top;
    return width > 0 && height > 0 && width <= std::numeric_limits<int>::max() &&
           height <= std::numeric_limits<int>::max();
}
}
LayoutExecution LayoutExecutor::submit(
    const std::array<WindowMoveTarget, 2>& targets) const {
    LayoutExecution result;
    for (int i = 0; i < 2; ++i) result.windows[i].target = targets[i];
    const auto scan = WindowsWindowScanner(api_).scan_with_status();
    if (!scan.complete) {
        for (auto& report : result.windows) report.observation = MoveObservation::ScanIncomplete;
        return result;
    }
    if (!targets[0].window || targets[0].window == targets[1].window ||
        !valid(targets[0].bounds) || !valid(targets[1].bounds)) {
        for (auto& report : result.windows)
            report.request = {WindowOperationStatus::InvalidArgument, 0};
        return result;
    }
    bool missing = false;
    for (auto& report : result.windows) {
        if (!find(scan, report.target)) {
            report.request = {WindowOperationStatus::InvalidWindow, 0};
            report.observation = MoveObservation::Missing;
            missing = true;
        }
    }
    if (missing) return result;
    result.submitted = true;
    for (auto& report : result.windows) {
        // Recheck PID immediately before each operation (not a durable identity).
        if (api_.get_process_id(report.target.window) != report.target.process_id)
            report.request = {WindowOperationStatus::InvalidWindow, 0};
        else report.request = api_.set_window_bounds(report.target.window, report.target.bounds);
    }
    observe(result);
    return result;
}
void LayoutExecutor::observe(LayoutExecution& execution) const {
    if (!execution.submitted) return;
    const auto scan = WindowsWindowScanner(api_).scan_with_status();
    for (auto& report : execution.windows) {
        if (!scan.complete) { report.observation = MoveObservation::ScanIncomplete; continue; }
        const auto* info = find(scan, report.target);
        if (!info) { report.observation = MoveObservation::Missing; continue; }
        report.actual = info->bounds;
        report.observation = equal(info->bounds, report.target.bounds)
            ? MoveObservation::Reached : MoveObservation::DifferentBounds;
    }
}
} // namespace crossdesk
