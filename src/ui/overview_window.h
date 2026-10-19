#pragma once

#include "overview_types.h"

#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace crossdesk::ui {

class WorkspaceService;

// 总览窗口：顶部 Workspace 条 + 左侧逐窗口列表。
// 它不持有任何归属状态，只把快照画出来，把用户动作翻译成 service 命令。
class OverviewWindow final : public QWidget {
    Q_OBJECT

public:
    explicit OverviewWindow(WorkspaceService* service, QWidget* parent = nullptr);

private slots:
    void applySnapshot(const crossdesk::ui::OverviewSnapshot& snapshot);
    void onOperationFinished(crossdesk::ui::WindowId window, bool success, const QString& message);

private:
    void buildUi();
    void buildDebugActions();
    void rebuildWorkspaceBar(const OverviewSnapshot& snapshot);
    void rebuildWindowList(const OverviewSnapshot& snapshot);
    void updateListCaption();
    void onSelectionChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void onListActivated(QListWidgetItem* item);
    void onMoveRequested();
    void onToggleShowAll(bool checked);
    void updateSelectionUi();
    void updateStatus(const QString& text, bool isError = false);

    QString workspaceName(const OverviewSnapshot& snapshot, const WorkspaceId& id) const;
    const WindowItem* findWindow(const OverviewSnapshot& snapshot, WindowId id) const;

    WorkspaceService* m_service = nullptr;

    QHBoxLayout* m_workspaceLayout = nullptr;
    QHBoxLayout* m_footerLayout = nullptr;
    QButtonGroup* m_workspaceGroup = nullptr;
    QLabel* m_listCaption = nullptr;
    QLabel* m_statusLabel = nullptr;
    QListWidget* m_windowList = nullptr;
    QPushButton* m_moveButton = nullptr;
    QPushButton* m_showAllButton = nullptr;

    // 最近一次收到的快照副本。界面只读它，不拿它当第二份真相。
    OverviewSnapshot m_lastSnapshot;

    WindowId m_selectedWindow = 0;
    bool m_hasSelection = false;

    // 是否显示全部工作区的窗口（"查看所有窗口"）。
    bool m_showAll = false;

    // 切换"查看所有窗口"时会重建列表，此时不应把"选中项不在当前页"
    // 误报成"窗口被移走/被关闭"。
    bool m_suppressSelectionNotice = false;
};

}  // namespace crossdesk::ui
