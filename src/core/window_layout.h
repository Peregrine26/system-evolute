#pragma once

#include <string>

#include "core/core_types.h"
#include "window_info.h"

// ============================================================================
// Delivery 3: two-window layout.
//
// Input: the work area of ONE monitor (taskbar already excluded) and a layout
// mode. Output: two WindowBounds in exactly the same units as the input; the
// core performs no DPI conversion and never guesses the screen size.
//
// Rules:
//   * a fixed 12-unit gap between the two windows;
//   * subtract the gap first, compute the left width with integer arithmetic,
//     and give the remainder to the right window, so both windows plus the gap
//     always add up to the work area width with no 1-pixel seam;
//   * intervals are half-open, so the right window's right edge equals the work
//     area's right edge and nothing exceeds it;
//   * a work area whose origin is not (0, 0) is supported (a second monitor to
//     the left of the primary one has negative coordinates);
//   * an invalid or too narrow area fails outright instead of returning half a
//     result.
// ============================================================================

namespace crossdesk {

// Fixed gap between the two windows, in the same units as the coordinates.
constexpr int kWindowGapUnits = 12;

enum class LayoutMode {
    Equal5050 = 0,  // even split
    Primary6040,    // left window is the primary one
};

enum class LayoutError {
    None = 0,
    InvalidWorkArea,  // non-positive width or height, or a negative gap
    TooNarrow,        // less than 2 units left after the gap
};

struct LayoutResult {
    LayoutError error = LayoutError::None;
    std::string message;
    WindowBounds left;
    WindowBounds right;

    bool ok() const { return error == LayoutError::None; }
};

// Pure function: the same (workArea, mode, gap) always yields the same result.
LayoutResult computeLayout(
    const WindowBounds& workArea,
    LayoutMode mode,
    int gap = kWindowGapUnits
);

// Display name of the mode, ready for the UI.
const char* layoutModeName(LayoutMode mode);

}  // namespace crossdesk
