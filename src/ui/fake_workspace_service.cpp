#include "fake_workspace_service.h"

#include <algorithm>

namespace crossdesk::ui {

namespace {
constexpr WindowId kNoWindow = 0;
}

FakeWorkspaceService::FakeWorkspaceService(QObject* parent)
    : WorkspaceService(parent) {
    // snapshotChanged 用队列连接投递给界面，自定义类型必须先注册，
    // 否则运行时只会打印 "Cannot queue arguments of type" 然后信号被丢掉。
    qRegisterMetaType<OverviewSnapshot>("crossdesk::ui::OverviewSnapshot");

    // 两个 Workspace，六个窗口，故意包含三种"麻烦情况"：
    // 用户自己最小化的窗口、受限窗口、以及稍后会被外部关闭的窗口。
    m_workspaces = {
        {QStringLiteral("base"), QStringLiteral("基础桌面"), 0},
        {QStringLiteral("study"), QStringLiteral("学习"), 0},
    };
    m_current = QStringLiteral("base");

    m_windows = {
        {1001, QStringLiteral("技术文档 - Chrome"), QStringLiteral("Chrome"),
         QStringLiteral("base"), ControlState::Manageable, Visibility::Visible, {}},
        {1002, QStringLiteral("项目资料"), QStringLiteral("资源管理器"),
         QStringLiteral("base"), ControlState::Manageable, Visibility::Visible, {}},
        {1003, QStringLiteral("需求草稿.txt - 记事本"), QStringLiteral("记事本"),
         QStringLiteral("base"), ControlState::Manageable, Visibility::MinimizedByUser, {}},
        {1004, QStringLiteral("CrossDesk - Visual Studio Code"), QStringLiteral("VS Code"),
         QStringLiteral("study"), ControlState::Manageable, Visibility::Visible, {}},
        {1005, QStringLiteral("微信"), QStringLiteral("WeChat"),
         QStringLiteral("study"), ControlState::Manageable, Visibility::Visible, {}},
        {1006, QStringLiteral("系统设置"), QStringLiteral("SystemSettings"),
         QStringLiteral("study"), ControlState::Restricted, Visibility::Visible, {}},
    };

    rebuildCounts();
}

OverviewSnapshot FakeWorkspaceService::snapshot() const {
    OverviewSnapshot result;
    result.workspaces = m_workspaces;
    result.currentWorkspaceId = m_current;
    for (const WindowItem& window : m_windows) {
        if (window.workspaceId == m_current) {
            result.currentWindows.push_back(window);
        }
    }
    return result;
}

void FakeWorkspaceService::switchWorkspace(const WorkspaceId& workspaceId) {
    if (!findWorkspace(workspaceId)) {
        emit operationFinished(kNoWindow, false,
                               QStringLiteral("目标 Workspace 已不存在，请刷新后重试。"));
        return;
    }
    if (m_current == workspaceId) {
        return;
    }
    m_current = workspaceId;
    rebuildCounts();
    notify();
    emit operationFinished(kNoWindow, true,
                           QStringLiteral("已切换到「%1」。")
                               .arg(findWorkspace(m_current)->name));
}

void FakeWorkspaceService::moveWindowToWorkspace(WindowId window, const WorkspaceId& workspaceId) {
    WindowItem* item = findWindow(window);
    if (!item) {
        emit operationFinished(window, false,
                               QStringLiteral("该窗口已关闭，操作没有执行。"));
        return;
    }
    if (!findWorkspace(workspaceId)) {
        emit operationFinished(window, false,
                               QStringLiteral("目标 Workspace 已不存在，窗口归属未改变。"));
        return;
    }
    if (item->workspaceId == workspaceId) {
        emit operationFinished(window, true, QStringLiteral("窗口已经在该 Workspace。"));
        return;
    }

    item->workspaceId = workspaceId;
    rebuildCounts();

    if (item->control == ControlState::Restricted) {
        // 归属变了，但显示状态没跟着变——这两种事实必须分开显示。
        item->failureMessage = QStringLiteral("归属已变更，显示状态未完成（受限窗口）。");
        notify();
        emit operationFinished(window, false,
                               QStringLiteral("「%1」归属已变更，但显示状态未完成，可重试。")
                                   .arg(item->title));
        return;
    }

    item->failureMessage.clear();
    notify();
    emit operationFinished(window, true,
                           QStringLiteral("「%1」已移动到「%2」。")
                               .arg(item->title, findWorkspace(workspaceId)->name));
}

void FakeWorkspaceService::activateWindow(WindowId window) {
    WindowItem* item = findWindow(window);
    if (!item) {
        emit operationFinished(window, false,
                               QStringLiteral("该窗口已关闭，无法定位。"));
        return;
    }
    if (item->control == ControlState::Restricted) {
        item->failureMessage = QStringLiteral("系统拒绝置前，可改用「查看所有窗口」找到它。");
        notify();
        emit operationFinished(window, false, item->failureMessage);
        return;
    }

    if (m_current != item->workspaceId) {
        m_current = item->workspaceId;
        rebuildCounts();
    }
    item->failureMessage.clear();
    notify();
    emit operationFinished(window, true,
                           QStringLiteral("已切到「%1」并尝试置前「%2」。")
                               .arg(findWorkspace(m_current)->name, item->title));
}

void FakeWorkspaceService::retryFailedOperations() {
    int cleared = 0;
    for (WindowItem& window : m_windows) {
        if (!window.failureMessage.isEmpty()) {
            window.failureMessage.clear();
            ++cleared;
        }
    }
    notify();
    emit operationFinished(kNoWindow, true,
                           cleared == 0 ? QStringLiteral("没有需要重试的操作。")
                                        : QStringLiteral("已重试 %1 项。").arg(cleared));
}

void FakeWorkspaceService::simulateWindowClosed(WindowId window) {
    const auto it = std::remove_if(m_windows.begin(), m_windows.end(),
                                   [window](const WindowItem& item) { return item.id == window; });
    if (it == m_windows.end()) {
        return;
    }
    m_windows.erase(it, m_windows.end());
    rebuildCounts();
    notify();
}

void FakeWorkspaceService::simulateOperationFailure(WindowId window, const QString& message) {
    if (WindowItem* item = findWindow(window)) {
        item->failureMessage = message;
        notify();
    }
}

void FakeWorkspaceService::rebuildCounts() {
    for (WorkspaceItem& workspace : m_workspaces) {
        workspace.windowCount =
            static_cast<int>(std::count_if(m_windows.cbegin(), m_windows.cend(),
                                           [&workspace](const WindowItem& item) {
                                               return item.workspaceId == workspace.id;
                                           }));
    }
}

void FakeWorkspaceService::notify() {
    emit snapshotChanged(snapshot());
}

WindowItem* FakeWorkspaceService::findWindow(WindowId window) {
    for (WindowItem& item : m_windows) {
        if (item.id == window) {
            return &item;
        }
    }
    return nullptr;
}

const WorkspaceItem* FakeWorkspaceService::findWorkspace(const WorkspaceId& workspaceId) const {
    for (const WorkspaceItem& workspace : m_workspaces) {
        if (workspace.id == workspaceId) {
            return &workspace;
        }
    }
    return nullptr;
}

}  // namespace crossdesk::ui
