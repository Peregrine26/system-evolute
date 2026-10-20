#pragma once

#include <string>

#include "core/core_types.h"
#include "core/window_filter.h"
#include "window_info.h"

// ============================================================================
// Delivery 2: selection validation.
//
// Input: the latest scan (with its complete flag) and the two window ids the
// user picked. Output: whether the layout may proceed, and if not, one sentence
// that C can show as-is.
//
// Three rules:
//   1. the two ids must differ;
//   2. both windows must still be candidates under the *current* scan result,
//      not merely "present at some point";
//   3. when the scan is incomplete, "not found" must never be reported as
//      "closed" - tell the user to refresh instead.
//
// Rule 3 matters most: treating "not seen" as "does not exist" makes the UI
// claim a window is closed while it is clearly still on screen.
// ============================================================================

namespace crossdesk {

enum class SelectionError {
    None = 0,        // validation passed
    SameWindow,      // the same window was picked twice
    ScanIncomplete,  // not found, but the scan itself was incomplete
    WindowGone,      // complete scan and still absent: treat as closed
    NotCandidate,    // still present, but hidden or untitled right now
};

struct SelectionResult {
    SelectionError error = SelectionError::None;
    std::string message;  // UTF-8 text, shown to the user as-is
    WindowInfo first;     // freshest snapshot of both windows, on success
    WindowInfo second;

    bool ok() const { return error == SelectionError::None; }
};

// Pure function: depends only on the arguments, keeps no previous selection and
// no global state. Mouse clicks and hotkeys therefore cannot disagree.
SelectionResult validateSelection(
    const ScanSnapshot& latest,
    WindowId first,
    WindowId second
);

}  // namespace crossdesk
