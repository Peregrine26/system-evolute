#include "core/window_layout.h"

namespace crossdesk {

namespace {

// Left window share in percent: 50 means 50%, 60 means 60%.
int leftPercent(LayoutMode mode) {
    switch (mode) {
        case LayoutMode::Equal5050:
            return 50;
        case LayoutMode::Primary6040:
            return 60;
    }
    return 50;
}

}  // namespace

const char* layoutModeName(LayoutMode mode) {
    switch (mode) {
        case LayoutMode::Equal5050:
            return "等宽 50:50";
        case LayoutMode::Primary6040:
            return "左侧主窗口 60:40";
    }
    return "未知模式";
}

LayoutResult computeLayout(
    const WindowBounds& workArea,
    LayoutMode mode,
    int gap
) {
    LayoutResult result;

    // 1) Is the input usable at all?
    if (boundsWidth(workArea) <= 0 || boundsHeight(workArea) <= 0) {
        result.error = LayoutError::InvalidWorkArea;
        result.message = "工作区矩形无效（宽或高不是正数），无法排布";
        return result;
    }
    if (gap < 0) {
        result.error = LayoutError::InvalidWorkArea;
        result.message = "窗口间距不能为负数";
        return result;
    }

    // 2) After the gap, are there still two positive widths to fill? A 13-unit
    //    area with a 12-unit gap leaves 1 unit, which is not a two-window
    //    layout, so it fails instead of producing a zero-width window.
    const int available = boundsWidth(workArea) - gap;
    if (available < 2) {
        result.error = LayoutError::TooNarrow;
        result.message = "可用宽度不足，放不下两个窗口（至少需要 " +
                         std::to_string(gap + 2) + " 个单位）";
        return result;
    }

    // 3) Integer arithmetic: multiply before dividing to avoid losing precision,
    //    and leave the remainder to the right window.
    const long long numerator =
        static_cast<long long>(available) * leftPercent(mode);
    const int leftWidth = static_cast<int>(numerator / 100);
    const int rightWidth = available - leftWidth;

    // 4) Build both rectangles by adding to edges; never convert width back into
    //    an edge, so no rounding seam can appear.
    result.left = WindowBounds{
        workArea.left,
        workArea.top,
        workArea.left + leftWidth,
        workArea.bottom,
    };
    result.right = WindowBounds{
        result.left.right + gap,
        workArea.top,
        result.left.right + gap + rightWidth,
        workArea.bottom,
    };

    result.error = LayoutError::None;
    result.message = "已计算两窗布局";
    return result;
}

}  // namespace crossdesk
