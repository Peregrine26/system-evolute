#pragma once

// 只读的实时数据源：用 A 的扫描器读真实窗口，喂给界面。
//
// 它解决的是"界面能不能在真实数据下工作"，不解决"怎么管理窗口"：
//   - 只调 WindowApiAdapter 的查询接口，不隐藏、不恢复、不置前；
//   - 不决定归属，所有窗口都挂在唯一的工作区下，归属规则仍归 B；
//   - 命令一律回失败并说明原因，绝不假装成功。
//
// 等 B 的 WorkspaceService 实现就绪后，这个类整体退场，界面代码不动。

#include "workspace_service.h"

#include <QString>
#include <QTimer>

#include <cstdint>
#include <memory>

namespace crossdesk {
class Win32ApiAdapter;
class WindowsWindowScanner;
}  // namespace crossdesk

namespace crossdesk::ui {

class LiveWorkspaceService : public WorkspaceService {
    Q_OBJECT

public:
    explicit LiveWorkspaceService(QObject* parent = nullptr);
    ~LiveWorkspaceService() override;

    OverviewSnapshot snapshot() const override;
    void switchWorkspace(const WorkspaceId& workspaceId) override;
    void moveWindowToWorkspace(WindowId window, const WorkspaceId& workspaceId) override;
    void activateWindow(WindowId window) override;
    void retryFailedOperations() override;

    // 立即扫一次，不等到下一个定时周期。测试和"手动刷新"用。
    void refreshNow();

private:
    void refresh();

    // 上一次扫描的指纹。窗口集合没变就不发 snapshotChanged，
    // 否则界面每秒都会被清空重建一次，选中项会不停丢失。
    QString m_signature;

    OverviewSnapshot m_snapshot;
    QTimer m_timer;
    std::uint32_t m_selfPid = 0;

    std::unique_ptr<crossdesk::Win32ApiAdapter> m_api;
    std::unique_ptr<crossdesk::WindowsWindowScanner> m_scanner;
};

}  // namespace crossdesk::ui
