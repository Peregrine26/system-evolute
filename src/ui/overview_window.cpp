#include "overview_window.h"

#include "workspace_service.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QStringList>
#include <QVariant>
#include <QVBoxLayout>

namespace crossdesk::ui {

namespace {
constexpr int kWindowIdRole = Qt::UserRole + 1;

QString describe(const WindowItem& window) {
    QStringList parts;
    parts << window.appName;
    switch (window.control) {
        case ControlState::Manageable:
            break;
        case ControlState::Restricted:
            parts << QStringLiteral("受限");
            break;
    }
    switch (window.visibility) {
        case Visibility::Visible:
            break;
        case Visibility::HiddenByCrossDesk:
            parts << QStringLiteral("已隐藏");
            break;
        case Visibility::MinimizedByUser:
            parts << QStringLiteral("最小化");
            break;
    }
    return parts.join(QStringLiteral(" · "));
}
}  // namespace

OverviewWindow::OverviewWindow(WorkspaceService* service, QWidget* parent)
    : QWidget(parent), m_service(service) {
    buildUi();

    // 队列连接：界面重建永远排到当前事件之后。
    // 否则"点击 Workspace 按钮 -> 服务同步发信号 -> 重建界面"会在按钮自己的
    // 点击处理函数里把它自己删掉，这是新手最常见的一类崩溃。
    connect(m_service, &WorkspaceService::snapshotChanged, this,
            &OverviewWindow::applySnapshot, Qt::QueuedConnection);
    connect(m_service, &WorkspaceService::operationFinished, this,
            &OverviewWindow::onOperationFinished, Qt::QueuedConnection);

    applySnapshot(m_service->snapshot());
}

void OverviewWindow::buildUi() {
    setWindowTitle(QStringLiteral("CrossDesk 总览"));
    resize(1180, 720);
    setStyleSheet(QStringLiteral(
        "QWidget { background: #f5f5f7; color: #1d1d1f; font-size: 13px; }"
        "QPushButton[role=\"workspace\"] {"
        "  background: #e8e8ed; border: 1px solid #d2d2d7; border-radius: 10px;"
        "  padding: 10px 18px; text-align: left; min-width: 150px; }"
        "QPushButton[role=\"workspace\"]:checked {"
        "  background: #ffffff; border: 2px solid #0071e3; color: #0071e3; }"
        "QPushButton[role=\"action\"] {"
        "  background: #ffffff; border: 1px solid #d2d2d7; border-radius: 8px;"
        "  padding: 6px 14px; }"
        "QListWidget { background: #ffffff; border: 1px solid #d2d2d7;"
        "  border-radius: 12px; outline: none; }"
        "QListWidget::item { padding: 10px 12px; border-bottom: 1px solid #f0f0f2; }"
        "QListWidget::item:selected { background: #0071e3; color: #ffffff; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(16);

    // ---- 顶部：Workspace 条 ----
    auto* workspaceBar = new QHBoxLayout();
    workspaceBar->setSpacing(12);
    m_workspaceLayout = workspaceBar;
    m_workspaceGroup = new QButtonGroup(this);
    m_workspaceGroup->setExclusive(true);
    root->addLayout(workspaceBar);

    // ---- 中部：左侧窗口列表 + 右侧占位 ----
    auto* body = new QHBoxLayout();
    body->setSpacing(16);

    m_windowList = new QListWidget(this);
    m_windowList->setFixedWidth(380);
    m_windowList->setFocusPolicy(Qt::StrongFocus);
    m_windowList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_windowList, &QListWidget::itemActivated, this, &OverviewWindow::onListActivated);
    connect(m_windowList, &QListWidget::itemClicked, this, &OverviewWindow::onListActivated);
    body->addWidget(m_windowList);

    auto* placeholder = new QLabel(QStringLiteral("缩略图区域（后续增强，先不做）"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet(QStringLiteral(
        "background:#ffffff; border:1px dashed #d2d2d7; border-radius:12px; color:#86868b;"));
    body->addWidget(placeholder, 1);
    root->addLayout(body, 1);

    // ---- 底部：状态与入口 ----
    auto* footer = new QHBoxLayout();
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#6e6e73;"));
    footer->addWidget(m_statusLabel, 1);

    auto* retry = new QPushButton(QStringLiteral("重试失败项"), this);
    retry->setProperty("role", "action");
    connect(retry, &QPushButton::clicked, this, [this] { m_service->retryFailedOperations(); });
    footer->addWidget(retry);

    auto* showAll = new QPushButton(QStringLiteral("查看所有窗口"), this);
    showAll->setProperty("role", "action");
    connect(showAll, &QPushButton::clicked, this, [this] {
        updateStatus(QStringLiteral("「查看所有窗口」尚未实现（下一步）。"));
    });
    footer->addWidget(showAll);
    root->addLayout(footer);

    m_windowList->setFocus();
}

void OverviewWindow::applySnapshot(const OverviewSnapshot& snapshot) {
    rebuildWorkspaceBar(snapshot);
    rebuildWindowList(snapshot);

    QString currentName;
    for (const WorkspaceItem& workspace : snapshot.workspaces) {
        if (workspace.id == snapshot.currentWorkspaceId) {
            currentName = workspace.name;
        }
    }
    updateStatus(QStringLiteral("当前：%1 · %2 个窗口")
                     .arg(currentName)
                     .arg(snapshot.currentWindows.size()));
}

void OverviewWindow::rebuildWorkspaceBar(const OverviewSnapshot& snapshot) {
    while (QLayoutItem* item = m_workspaceLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            if (auto* button = qobject_cast<QPushButton*>(widget)) {
                m_workspaceGroup->removeButton(button);
            }
            // 先隐藏再延迟删除：此刻可能正处在该按钮的点击处理函数中。
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    for (const WorkspaceItem& workspace : snapshot.workspaces) {
        auto* button = new QPushButton(
            QStringLiteral("%1\n%2 个窗口").arg(workspace.name).arg(workspace.windowCount), this);
        button->setProperty("role", "workspace");
        button->setCheckable(true);
        button->setChecked(workspace.id == snapshot.currentWorkspaceId);
        const WorkspaceId id = workspace.id;
        connect(button, &QPushButton::clicked, this, [this, id] { m_service->switchWorkspace(id); });
        m_workspaceGroup->addButton(button);
        m_workspaceLayout->addWidget(button);
    }
    m_workspaceLayout->addStretch(1);
}

void OverviewWindow::rebuildWindowList(const OverviewSnapshot& snapshot) {
    const WindowId previouslySelected = m_hasSelection ? m_selectedWindow : 0;

    m_windowList->clear();

    if (snapshot.currentWindows.isEmpty()) {
        auto* empty = new QListWidgetItem(QStringLiteral("这个 Workspace 还没有窗口"));
        empty->setFlags(Qt::NoItemFlags);
        m_windowList->addItem(empty);
        m_hasSelection = false;
        return;
    }

    QListWidgetItem* toSelect = nullptr;
    for (const WindowItem& window : snapshot.currentWindows) {
        QString text = window.title.isEmpty() ? QStringLiteral("（无标题窗口）") : window.title;
        text += QStringLiteral("\n") + describe(window);
        if (!window.failureMessage.isEmpty()) {
            text += QStringLiteral("\n! ") + window.failureMessage;
        }

        auto* item = new QListWidgetItem(text);
        item->setData(kWindowIdRole, QVariant::fromValue<qulonglong>(window.id));
        m_windowList->addItem(item);

        if (window.id == previouslySelected) {
            toSelect = item;
        }
    }

    if (toSelect) {
        m_windowList->setCurrentItem(toSelect);
        return;
    }

    // 之前选中的窗口已经不在了：清除选择，并且不要把下一条命令转给相邻窗口。
    if (m_hasSelection) {
        m_hasSelection = false;
        m_selectedWindow = 0;
        updateStatus(QStringLiteral("选中的窗口已经关闭，选择已清除。"), true);
    }
}

void OverviewWindow::onListActivated(QListWidgetItem* item) {
    if (!item) {
        return;
    }
    const QVariant data = item->data(kWindowIdRole);
    if (!data.isValid()) {
        return;  // 空列表占位项
    }
    const auto id = static_cast<WindowId>(data.toULongLong());
    m_selectedWindow = id;
    m_hasSelection = true;
    m_service->activateWindow(id);
}

void OverviewWindow::onOperationFinished(WindowId, bool success, const QString& message) {
    updateStatus(message, !success);
}

void OverviewWindow::updateStatus(const QString& text, bool isError) {
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color:#c1121f;")
                                          : QStringLiteral("color:#6e6e73;"));
}

}  // namespace crossdesk::ui
