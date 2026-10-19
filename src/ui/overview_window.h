#pragma once

#include "overview_types.h"

#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QListWidgetItem;

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
    void onListActivated(QListWidgetItem* item);
    void rebuildWorkspaceBar(const OverviewSnapshot& snapshot);
    void rebuildWindowList(const OverviewSnapshot& snapshot);
    void updateStatus(const QString& text, bool isError = false);

    WorkspaceService* m_service = nullptr;

    QHBoxLayout* m_workspaceLayout = nullptr;
    QButtonGroup* m_workspaceGroup = nullptr;
    QListWidget* m_windowList = nullptr;
    QLabel* m_statusLabel = nullptr;

    WindowId m_selectedWindow = 0;
    bool m_hasSelection = false;
};

}  // namespace crossdesk::ui
