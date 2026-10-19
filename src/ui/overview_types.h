#pragma once

// UI 层使用的数据结构。这里刻意不放任何 HWND / X11 类型，
// 也不放 B 的 WorkspaceManager 内部结构：UI 只认识"快照"。

#include <QMetaType>
#include <QString>
#include <QVector>

#include <cstdint>

namespace crossdesk::ui {

// 窗口标识只在当前会话有效，跨重启不保证同一个窗口还是同一个 id。
using WindowId = std::uintptr_t;

using WorkspaceId = QString;

enum class ControlState {
    Manageable,  // CrossDesk 可以隐藏 / 恢复 / 置前
    Restricted,  // 能看到，但平台拒绝操作（权限、系统窗口、被其他程序抢占）
};

enum class Visibility {
    Visible,
    HiddenByCrossDesk,  // 由 CrossDesk 成功隐藏，只有它能负责恢复
    MinimizedByUser,    // 用户自己最小化，CrossDesk 不擅自恢复
};

struct WindowItem {
    WindowId id = 0;
    QString title;
    QString appName;
    WorkspaceId workspaceId;
    ControlState control = ControlState::Manageable;
    Visibility visibility = Visibility::Visible;
    // 最近一次针对该窗口的操作失败时的提示；为空表示没有失败。
    QString failureMessage;
};

struct WorkspaceItem {
    WorkspaceId id;
    QString name;
    int windowCount = 0;
};

// 一次完整刷新所需的全部状态。UI 永远只读这份快照，
// 不持有可变对象，也不自己推算归属。
struct OverviewSnapshot {
    QVector<WorkspaceItem> workspaces;
    WorkspaceId currentWorkspaceId;
    // 全部窗口，不只是当前工作区的。UI 按 workspaceId 过滤出当前页，
    // 这样"窗口消失了"和"窗口被挪到别的工作区了"才能区分开。
    QVector<WindowItem> windows;
};

}  // namespace crossdesk::ui

Q_DECLARE_METATYPE(crossdesk::ui::OverviewSnapshot)
