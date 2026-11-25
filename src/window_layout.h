#pragma once
#include "platform/layout_executor.h"
#include <array>

namespace crossdesk {
enum class TwoWindowMode { Equal, MainLeft };
enum class LayoutPlanError { None, ScanIncomplete, InvalidSelection, InvalidWorkArea, WindowMissing, IdentityChanged, WindowChanged, WorkAreaChanged };
struct WindowIdentity { NativeWindowHandle handle = 0; std::uint32_t process_id = 0; };
struct TwoWindowPlan {
    std::array<WindowMoveTarget, 2> targets;
    std::array<WindowBounds, 2> sources;
    WindowBounds work_area;
    TwoWindowMode mode = TwoWindowMode::Equal;
};
struct LayoutPlanResult { LayoutPlanError error = LayoutPlanError::InvalidSelection; TwoWindowPlan plan; bool valid() const { return error == LayoutPlanError::None; } };
LayoutPlanResult planTwoWindows(const WindowScanResult&, const std::array<WindowIdentity, 2>&,
                               const WindowBounds&, TwoWindowMode, std::uint32_t own_process_id);
LayoutPlanError validateTwoWindowPlan(const TwoWindowPlan&, const WindowScanResult&,
                                     const WindowBounds& current_work_area, std::uint32_t own_process_id);
}
