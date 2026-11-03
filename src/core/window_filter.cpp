#include "core/window_filter.h"

namespace crossdesk {

namespace {

// A title that consists only of blanks is the same as no title for the user.
// wchar_t is UTF-16 on Windows and UTF-32 on Linux, so every character below is
// a single code unit on both platforms and no decoding is needed.
bool isBlankCharacter(wchar_t c) {
    switch (c) {
        case L' ':
        case L'\t':
        case L'\r':
        case L'\n':
        case L'\v':
        case L'\f':
        case 0x00A0:  // no-break space
        case 0x3000:  // ideographic space (full-width, common in Chinese titles)
            return true;
        default:
            return false;
    }
}

}  // namespace

bool hasVisibleTitle(const std::wstring& title) {
    for (const wchar_t c : title) {
        if (!isBlankCharacter(c)) {
            return true;
        }
    }
    return false;
}

bool isCandidate(const WindowInfo& window) {
    return window.visible && hasVisibleTitle(window.title);
}

std::vector<WindowInfo> selectCandidates(
    const std::vector<WindowInfo>& scanned
) {
    std::vector<WindowInfo> candidates;
    candidates.reserve(scanned.size());

    for (const WindowInfo& window : scanned) {
        if (isCandidate(window)) {
            candidates.push_back(window);
        }
    }

    return candidates;
}

}  // namespace crossdesk
