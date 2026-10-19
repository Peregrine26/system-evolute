#pragma once

// UI 与核心之间唯一的接口。
//
// 约定：
// 1. UI 只调用这里的命令，不直接调用 Win32 / X11，也不保存第二份归属真相。
// 2. 命令返回不等于成功。真正的结果通过 operationFinished 回来，
//    由 B 观察到的实际窗口状态决定。
// 3. 状态变化统一走 snapshotChanged，UI 整体重建显示，不做增量修改。

#include "overview_types.h"

#include <QObject>

namespace crossdesk::ui {

class WorkspaceService : public QObject {
    Q_OBJECT

public:
    explicit WorkspaceService(QObject* parent = nullptr) : QObject(parent) {}
    ~WorkspaceService() override = default;

    // 当前完整状态。可能被 UI 在任意时刻调用，必须足够快。
    virtual OverviewSnapshot snapshot() const = 0;

    // 切换当前 Workspace。部分窗口失败时仍然切换，失败项留在快照里。
    virtual void switchWorkspace(const WorkspaceId& workspaceId) = 0;

    // 改变窗口归属。改完立即尝试让显示状态匹配新归属（由 B 决定怎么做）。
    virtual void moveWindowToWorkspace(WindowId window, const WorkspaceId& workspaceId) = 0;

    // 选中窗口：收起总览、切到其所属 Workspace、尝试展开或置前。
    virtual void activateWindow(WindowId window) = 0;

    // 重试所有失败项。
    virtual void retryFailedOperations() = 0;

signals:
    // 任何会让界面变化的状态更新。
    void snapshotChanged(const crossdesk::ui::OverviewSnapshot& snapshot);

    // 单个窗口操作的最终结果。success=false 时 message 要能直接显示给用户，
    // 并且必须说明"现在怎么找回 / 能不能重试"。
    void operationFinished(crossdesk::ui::WindowId window, bool success, const QString& message);
};

}  // namespace crossdesk::ui
