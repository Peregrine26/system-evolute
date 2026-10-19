#pragma once

// 假数据实现：不存在 B 的 WorkspaceManager 时，用这个把 UI 跑起来。
// 接真实实现时，只写一个新的 WorkspaceService 子类，
// UI 的代码一行都不用改。

#include "workspace_service.h"

namespace crossdesk::ui {

class FakeWorkspaceService final : public WorkspaceService {
    Q_OBJECT

public:
    explicit FakeWorkspaceService(QObject* parent = nullptr);

    OverviewSnapshot snapshot() const override;
    void switchWorkspace(const WorkspaceId& workspaceId) override;
    void moveWindowToWorkspace(WindowId window, const WorkspaceId& workspaceId) override;
    void activateWindow(WindowId window) override;
    void retryFailedOperations() override;

    // 只有假实现才有的调试入口：模拟窗口被外部关闭 / 模拟一次平台失败。
    void simulateWindowClosed(WindowId window);
    void simulateOperationFailure(WindowId window, const QString& message);

    // 恢复初始的 2 个 Workspace / 6 个窗口，用于反复演示。
    void reset();

private:
    void seedData();
    void rebuildCounts();
    void notify();
    WindowItem* findWindow(WindowId window);
    const WorkspaceItem* findWorkspace(const WorkspaceId& workspaceId) const;

    QVector<WorkspaceItem> m_workspaces;
    QVector<WindowItem> m_windows;
    WorkspaceId m_current;
};

}  // namespace crossdesk::ui
