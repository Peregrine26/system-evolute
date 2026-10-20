#include "core/window_selection.h"

namespace crossdesk {

namespace {

const WindowInfo* findById(const ScanSnapshot& scan, WindowId id) {
    for (const WindowInfo& window : scan.windows) {
        // The handle is the only key. Two windows with the same title, or from
        // the same process, are still two windows.
        if (window.native_handle == id) {
            return &window;
        }
    }
    return nullptr;
}

// Say which rule failed, so the user has something to act on.
std::string describeNotCandidate(const WindowInfo& window) {
    if (!window.visible) {
        // Not "minimized": a minimized window keeps WS_VISIBLE and therefore
        // stays a candidate, so this branch only covers actually hidden windows.
        return "该窗口当前不可见（可能已被隐藏），请先恢复显示";
    }
    if (!hasVisibleTitle(window.title)) {
        return "该窗口没有标题，无法确认哪一个是它";
    }
    return "该窗口当前不可用";
}

}  // namespace

SelectionResult validateSelection(
    const ScanSnapshot& latest,
    WindowId first,
    WindowId second
) {
    SelectionResult result;

    if (first == second) {
        result.error = SelectionError::SameWindow;
        result.message = "请选择两个不同的窗口";
        return result;
    }

    const WindowInfo* a = findById(latest, first);
    const WindowInfo* b = findById(latest, second);

    // "Present but unusable" is hard evidence and does not depend on whether the
    // scan was complete, so it is checked first.
    if (a != nullptr && !isCandidate(*a)) {
        result.error = SelectionError::NotCandidate;
        result.message = describeNotCandidate(*a);
        return result;
    }
    if (b != nullptr && !isCandidate(*b)) {
        result.error = SelectionError::NotCandidate;
        result.message = describeNotCandidate(*b);
        return result;
    }

    // "Absent from the scan" is only conclusive when the scan was complete.
    if (a == nullptr || b == nullptr) {
        if (!latest.complete) {
            result.error = SelectionError::ScanIncomplete;
            result.message =
                "扫描结果不完整，暂时无法确认窗口是否还在，请刷新后重试";
            return result;
        }

        result.error = SelectionError::WindowGone;
        result.message = "所选窗口已经关闭，请重新选择";
        return result;
    }

    result.error = SelectionError::None;
    result.message = "已选择两个窗口";
    result.first = *a;
    result.second = *b;
    return result;
}

}  // namespace crossdesk
