#pragma once

#include "platform/window_api_adapter.h"
#include "windows_window_scanner.h"

#include <array>

namespace crossdesk {
// B supplies these rectangles; this class never computes a layout.
struct WindowMoveTarget {
    NativeWindowHandle window = 0;
    std::uint32_t process_id = 0;
    WindowBounds bounds;
};
enum class MoveObservation { Pending, Reached, DifferentBounds, Missing, ScanIncomplete };
struct WindowMoveReport {
    WindowMoveTarget target;
    WindowOperationResult request;
    MoveObservation observation = MoveObservation::Pending;
    WindowBounds actual;
};
struct LayoutExecution {
    // False means validation failed: no window was moved.
    bool submitted = false;
    std::array<WindowMoveReport, 2> windows;
};

class LayoutExecutor {
public:
    explicit LayoutExecutor(const WindowApiAdapter& api) : api_(api) {}
    LayoutExecution submit(const std::array<WindowMoveTarget, 2>& targets) const;
    // Call after a short UI timer (e.g. 150 ms); no sleeps on the UI thread.
    // Request acceptance and observed geometry are deliberately separate.
    void observe(LayoutExecution& execution) const;
private:
    const WindowApiAdapter& api_;
};
} // namespace crossdesk
