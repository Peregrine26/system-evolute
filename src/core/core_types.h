#pragma once

#include <cstdint>
#include <vector>

#include "window_info.h"

// ============================================================================
// Shared data types for the platform-neutral core logic (B).
//
// Everything here is plain C++: no <windows.h>, no Qt, no window handles with
// platform meaning. The core receives WindowInfo values produced by A and
// returns numbers; it never calls the platform itself.
// ============================================================================

namespace crossdesk {

// Opaque window identifier used as a key by the core logic.
//
// Deliberately defined here instead of reusing NativeWindowHandle from
// platform/window_api_adapter.h: the core does not call the adapter, so it
// should not include that header. tests/core_tests.cpp holds a static_assert
// that this alias and NativeWindowHandle stay the same type, so the two cannot
// silently diverge.
using WindowId = std::uintptr_t;

// One desktop scan plus whether that scan may be trusted as complete.
//
// A window missing from `windows` has two very different causes: it was really
// closed, or this scan simply did not see it (window destroyed mid-enumeration,
// a query failed, enumeration returned early). Only `complete == true` allows
// the core to translate "not found" into "closed".
struct ScanSnapshot {
    std::vector<WindowInfo> windows;
    bool complete = true;
};

// WindowBounds comes from src/window_info.h and has no member functions, so the
// half-open-interval arithmetic lives here as free functions. Both bounds are
// half-open: width == right - left, and the right/bottom edge belongs to the
// next window, not to this one.
inline int boundsWidth(const WindowBounds& bounds) {
    return bounds.right - bounds.left;
}

inline int boundsHeight(const WindowBounds& bounds) {
    return bounds.bottom - bounds.top;
}

inline bool sameBounds(const WindowBounds& a, const WindowBounds& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right &&
           a.bottom == b.bottom;
}

}  // namespace crossdesk
