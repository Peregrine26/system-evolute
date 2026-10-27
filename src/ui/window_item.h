#pragma once

#include <QMetaType>
#include <QRect>
#include <QString>
#include <QVector>

namespace crossdesk::ui {

// 平台窗口标识。A 的 NativeWindowHandle 是 uintptr_t；UI 只把它当作不透明 ID
// 传递和比较，不解引用、不转成 HWND，也不包含 windows.h。
using WindowId = quintptr;

struct WindowItem {
    WindowId id = 0;
    QString title;
    QString processName;
    QRect bounds;            // 屏幕坐标；未知时 hasBounds 为 false
    bool hasBounds = false;
    QString failureMessage;  // 最近一次针对该窗口的失败说明，空表示没有
};

// 两种左右排列。枚举值本身就是 B 的布局函数的输入，不散落在界面代码里。
enum class LayoutMode {
    Equal,       // 等宽
    LeftMaster,  // 左侧主窗口 60 / 40
};

// 一个窗口的目标位置。由 B 计算，界面只负责画和往上报。
struct Placement {
    WindowId id = 0;
    QRect rect;
};

struct OperationResult {
    WindowId window = 0;  // 0 表示与具体窗口无关
    bool success = false;
    QString message;
};

}  // namespace crossdesk::ui

Q_DECLARE_METATYPE(crossdesk::ui::OperationResult)
