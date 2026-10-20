#include "layout_math.h"

#include <QtMath>

namespace crossdesk::ui {

bool computeLayout(const QRect& workArea,
                   int count,
                   LayoutMode mode,
                   int gap,
                   QVector<QRect>& out,
                   QString* error) {
    out.clear();

    const auto fail = [error](const QString& text) {
        if (error) {
            *error = text;
        }
        return false;
    };

    if (!workArea.isValid() || workArea.width() <= 0 || workArea.height() <= 0) {
        return fail(QStringLiteral("没有拿到屏幕可用区域，无法计算布局。"));
    }
    if (count != 2) {
        return fail(QStringLiteral("左右排列需要恰好两个窗口。"));
    }

    const int spacing = gap < 0 ? 0 : gap;
    const int usable = workArea.width() - spacing;

    // 先判宽度再判比例：否则 60:40 在窄屏上会算出 190 像素的右列，
    // 用户看到的是"窗口被压成一条"，而不是"这里放不下"。
    if (usable < 2 * kMinPaneWidth) {
        return fail(QStringLiteral("可用区域只有 %1 像素宽，放不下两个窗口。")
                        .arg(workArea.width()));
    }

    int leftWidth = (mode == LayoutMode::Equal)
                        ? usable / 2
                        : qRound(static_cast<qreal>(usable) * 0.6);
    int rightWidth = usable - leftWidth;

    // 整除误差或四舍五入可能让一侧差几个像素，兜底补齐，保证两列不重叠。
    if (leftWidth < kMinPaneWidth) {
        leftWidth = kMinPaneWidth;
        rightWidth = usable - leftWidth;
    }
    if (rightWidth < kMinPaneWidth) {
        rightWidth = kMinPaneWidth;
        leftWidth = usable - rightWidth;
    }
    if (leftWidth < kMinPaneWidth || rightWidth < kMinPaneWidth) {
        return fail(QStringLiteral("按当前比例分出的列太窄，放不下两个窗口。"));
    }

    const int top = workArea.top();
    const int height = workArea.height();

    out.push_back(QRect(workArea.left(), top, leftWidth, height));
    out.push_back(QRect(workArea.left() + leftWidth + spacing, top, rightWidth, height));
    return true;
}

}  // namespace crossdesk::ui
