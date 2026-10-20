#pragma once

#include <string>
#include <vector>

#include "core/core_types.h"
#include "window_info.h"

// ============================================================================
// Delivery 1: candidate filtering.
//
// Input: the scan result A produced after excluding shell surfaces (taskbar,
// desktop background, Start menu). Output: the windows that may appear in the
// overview list, in the original scan order.
//
// Two rules, both explainable and testable:
//   1. visible == true        - an invisible window cannot be picked or moved.
//   2. title is not blank     - an untitled window renders as an empty row the
//                               user cannot identify.
//
// Explicitly NOT done here:
//   * no filtering by process name: File Explorer and Windows Settings are real
//     user windows, and the task order forbids excluding system apps by name;
//   * no de-duplication by title or PID: two identical titles are two windows;
//   * no guessing which windows are shell surfaces - A owns that.
// ============================================================================

namespace crossdesk {

// True when the title has at least one non-blank character. Blank means spaces,
// tabs, line breaks, the full-width space U+3000 and the no-break space U+00A0.
bool hasVisibleTitle(const std::wstring& title);

// True when this single window may be offered to the user.
bool isCandidate(const WindowInfo& window);

// Filters a whole scan; the order of `scanned` is preserved.
std::vector<WindowInfo> selectCandidates(const std::vector<WindowInfo>& scanned);

}  // namespace crossdesk
