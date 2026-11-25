#include "window_layout.h"
#include <algorithm>
#include <limits>

namespace crossdesk {
namespace {
bool equal(const WindowBounds& a, const WindowBounds& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
const WindowInfo* candidate(const WindowScanResult& scan, NativeWindowHandle handle, std::uint32_t own) {
    const auto found = std::find_if(scan.windows.begin(), scan.windows.end(), [=](const auto& window) {
        return window.native_handle == handle && window.visible && !window.title.empty() && window.process_id != own;
    });
    return found == scan.windows.end() ? nullptr : &*found;
}
}
LayoutPlanResult planTwoWindows(const WindowScanResult& scan, const std::array<WindowIdentity, 2>& selected,
                               const WindowBounds& work, TwoWindowMode mode, std::uint32_t own) {
    LayoutPlanResult result;
    if (!scan.complete) { result.error = LayoutPlanError::ScanIncomplete; return result; }
    if (!selected[0].handle || !selected[1].handle || selected[0].handle == selected[1].handle ||
        !selected[0].process_id || !selected[1].process_id) return result;
    const long long width = static_cast<long long>(work.right) - work.left;
    const long long height = static_cast<long long>(work.bottom) - work.top;
    if (width < 14 || width > std::numeric_limits<int>::max() || height <= 0 || height > std::numeric_limits<int>::max()) {
        result.error = LayoutPlanError::InvalidWorkArea; return result;
    }
    for (int i = 0; i < 2; ++i) {
        const auto* window = candidate(scan, selected[i].handle, own);
        if (!window) { result.error = LayoutPlanError::WindowMissing; return result; }
        if (window->process_id != selected[i].process_id) { result.error = LayoutPlanError::IdentityChanged; return result; }
        result.plan.sources[i] = window->bounds;
        result.plan.targets[i].window = selected[i].handle;
        result.plan.targets[i].process_id = selected[i].process_id;
    }
    const int left = int((width - 12) * (mode == TwoWindowMode::MainLeft ? 60 : 50) / 100);
    result.plan.targets[0].bounds = {work.left, work.top, work.left + left, work.bottom};
    result.plan.targets[1].bounds = {work.left + left + 12, work.top, work.right, work.bottom};
    result.plan.mode = mode; result.plan.work_area = work;
    result.error = LayoutPlanError::None;
    return result;
}
LayoutPlanError validateTwoWindowPlan(const TwoWindowPlan& plan, const WindowScanResult& scan,
                                     const WindowBounds& work, std::uint32_t own) {
    if (!scan.complete) return LayoutPlanError::ScanIncomplete;
    if (!equal(plan.work_area, work)) return LayoutPlanError::WorkAreaChanged;
    std::array<WindowIdentity, 2> selection{{{plan.targets[0].window, plan.targets[0].process_id}, {plan.targets[1].window, plan.targets[1].process_id}}};
    const auto updated = planTwoWindows(scan, selection, work, plan.mode, own);
    if (!updated.valid()) return updated.error;
    for (int i = 0; i < 2; ++i) {
        if (!equal(plan.sources[i], updated.plan.sources[i])) return LayoutPlanError::WindowChanged;
        if (!equal(plan.targets[i].bounds, updated.plan.targets[i].bounds)) return LayoutPlanError::InvalidWorkArea;
    }
    return LayoutPlanError::None;
}
}
