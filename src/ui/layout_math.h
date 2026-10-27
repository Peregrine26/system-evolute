#pragma once

#include "window_item.h"

#include <QRect>
#include <QString>
#include <QVector>

namespace crossdesk::ui {

// 左右排列时每一列的最小宽度。再窄就没有"排列"的意义了。
inline constexpr int kMinPaneWidth = 320;

// 两列之间的固定间距，单位与屏幕坐标一致。
inline constexpr int kPaneGap = 12;

// B 的布局算法替换点。
//
// 这是 UI 里唯一一处"算位置"的代码，抽成不依赖界面的纯函数，原因是：
//   1. 它属于 B 的职责（等宽 / 60:40 / 边界校验），当前只是 UI 的临时实现；
//   2. 纯函数可以被单元测试直接调用，不需要真实桌面；
//   3. 换成 B 的实现时只改这一个文件，界面一行不动。
//
// workArea 必须与窗口位置使用同一个坐标系（由 A 提供）；这里不写死分辨率。
// 失败时返回 false，error 里给出能直接显示给用户的中文原因。
bool computeLayout(const QRect& workArea,
                   int count,
                   LayoutMode mode,
                   int gap,
                   QVector<QRect>& out,
                   QString* error);

}  // namespace crossdesk::ui
