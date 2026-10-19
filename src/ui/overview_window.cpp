#include "overview_window.h"

#include "fake_workspace_service.h"
#include "workspace_service.h"

#include <QAction>
#include <QButtonGroup>
#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLayoutItem>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPushButton>
#include <QStringList>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

namespace crossdesk::ui {

namespace {
constexpr int kWindowIdRole = Qt::UserRole + 1;

// 一行里的窗口说明。workspaceName 只在"查看所有窗口"模式下传，
// 因为那时列表混着多个工作区，必须标出每个窗口属于哪里。
QString describe(const WindowItem& window, const QString& workspaceName = QString()) {
    QStringList parts;
    parts << window.appName;
    if (!workspaceName.isEmpty()) {
        parts << workspaceName;
    }
    if (window.control == ControlState::Restricted) {
        parts << QStringLiteral("受限");
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
        "QPushButton[role=\"action\"], QToolButton[role=\"action\"] {"
        "  background: #ffffff; border: 1px solid #d2d2d7; border-radius: 8px;"
        "  padding: 6px 14px; }"
        "QPushButton[role=\"action\"]:disabled { color: #b0b0b5; }"
        "QPushButton[role=\"action\"]:checked {"
        "  background: #0071e3; border-color: #0071e3; color: #ffffff; }"
        "QListWidget { background: #ffffff; border: 1px solid #d2d2d7;"
        "  border-radius: 12px; outline: none; }"
        "QListWidget::item { padding: 10px 12px; border-bottom: 1px solid #f0f0f2; }"
        "QListWidget::item:selected { background: #0071e3; color: #ffffff; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(16);

    // ---- 顶部：Workspace 条 ----
    m_workspaceLayout = new QHBoxLayout();
    m_workspaceLayout->setSpacing(12);
    m_workspaceGroup = new QButtonGroup(this);
    m_workspaceGroup->setExclusive(true);
    root->addLayout(m_workspaceLayout);

    // ---- 中部：左侧窗口列表 + 右侧占位 ----
    auto* body = new QHBoxLayout();
    body->setSpacing(16);

    auto* leftColumn = new QWidget(this);
    leftColumn->setFixedWidth(380);
    auto* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    m_listCaption = new QLabel(leftColumn);
    m_listCaption->setStyleSheet(QStringLiteral("color:#6e6e73; font-weight:600;"));
    leftLayout->addWidget(m_listCaption);

    m_windowList = new QListWidget(leftColumn);
    m_windowList->setFocusPolicy(Qt::StrongFocus);
    m_windowList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_windowList, &QListWidget::currentItemChanged, this,
            &OverviewWindow::onSelectionChanged);
    connect(m_windowList, &QListWidget::itemActivated, this, &OverviewWindow::onListActivated);
    leftLayout->addWidget(m_windowList, 1);

    body->addWidget(leftColumn);

    auto* placeholder = new QLabel(QStringLiteral("缩略图区域（后续增强，先不做）"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet(QStringLiteral(
        "background:#ffffff; border:1px dashed #d2d2d7; border-radius:12px; color:#86868b;"));
    body->addWidget(placeholder, 1);
    root->addLayout(body, 1);

    // ---- 底部：状态与入口 ----
    auto* footer = new QHBoxLayout();
    m_footerLayout = footer;
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#6e6e73;"));
    m_statusLabel->setWordWrap(false);
    footer->addWidget(m_statusLabel, 1);

    m_moveButton = new QPushButton(QStringLiteral("移到其他工作区"), this);
    m_moveButton->setProperty("role", "action");
    m_moveButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    m_moveButton->setToolTip(QStringLiteral("Ctrl+M"));
    m_moveButton->setEnabled(false);
    connect(m_moveButton, &QPushButton::clicked, this, &OverviewWindow::onMoveRequested);
    footer->addWidget(m_moveButton);

    auto* retry = new QPushButton(QStringLiteral("重试失败项"), this);
    retry->setProperty("role", "action");
    connect(retry, &QPushButton::clicked, this, [this] { m_service->retryFailedOperations(); });
    footer->addWidget(retry);

    m_showAllButton = new QPushButton(QStringLiteral("查看所有窗口"), this);
    m_showAllButton->setProperty("role", "action");
    m_showAllButton->setCheckable(true);
    m_showAllButton->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    m_showAllButton->setToolTip(QStringLiteral("Ctrl+Shift+A"));
    connect(m_showAllButton, &QPushButton::toggled, this, &OverviewWindow::onToggleShowAll);
    footer->addWidget(m_showAllButton);

    root->addLayout(footer);

    buildDebugActions();

    m_windowList->setFocus();
}

// 假数据阶段专用：没有真实的窗口变化可以演示"窗口被关闭"和"列表空了"，
// 所以给一个调试入口。接 B 的真实实现后 qobject_cast 会失败，这段按钮不会出现。
void OverviewWindow::buildDebugActions() {
    auto* fake = qobject_cast<FakeWorkspaceService*>(m_service);
    if (!fake) {
        return;
    }

    auto* button = new QToolButton(this);
    button->setText(QStringLiteral("调试（假数据）"));
    button->setProperty("role", "action");
    button->setPopupMode(QToolButton::InstantPopup);

    auto* menu = new QMenu(button);

    QAction* closeSelected = menu->addAction(QStringLiteral("模拟关闭选中的窗口"));
    connect(closeSelected, &QAction::triggered, this, [this, fake] {
        if (!m_hasSelection) {
            updateStatus(QStringLiteral("先在列表里选中一个窗口，再模拟关闭。"), true);
            return;
        }
        fake->simulateWindowClosed(m_selectedWindow);
    });

    QAction* clearCurrent = menu->addAction(QStringLiteral("清空当前工作区"));
    connect(clearCurrent, &QAction::triggered, this, [this, fake] {
        // snapshot() 返回的是副本，所以在循环里接着发命令是安全的。
        const OverviewSnapshot snapshot = m_service->snapshot();
        for (const WindowItem& window : snapshot.windows) {
            if (window.workspaceId == snapshot.currentWorkspaceId) {
                fake->simulateWindowClosed(window.id);
            }
        }
    });

    QAction* reset = menu->addAction(QStringLiteral("重置假数据"));
    connect(reset, &QAction::triggered, this, [fake] { fake->reset(); });

    button->setMenu(menu);
    m_footerLayout->addWidget(button);
}

void OverviewWindow::applySnapshot(const OverviewSnapshot& snapshot) {
    m_lastSnapshot = snapshot;

    rebuildWorkspaceBar(snapshot);

    // 先写"当前在哪一页"的概括，再重建列表：
    // 如果列表发现"选中的窗口刚被移走 / 被关闭"，它会用更重要的提示覆盖这句话。
    // 顺序反过来（先重建再写概括）会把那条提示冲掉，用户就永远看不到"窗口已关闭"。
    QString currentName;
    int currentCount = 0;
    for (const WorkspaceItem& workspace : snapshot.workspaces) {
        if (workspace.id == snapshot.currentWorkspaceId) {
            currentName = workspace.name;
        }
    }
    for (const WindowItem& window : snapshot.windows) {
        if (window.workspaceId == snapshot.currentWorkspaceId) {
            ++currentCount;
        }
    }
    updateStatus(QStringLiteral("当前：%1 · %2 个窗口").arg(currentName).arg(currentCount));

    rebuildWindowList(snapshot);
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

    m_windowList->clear();  // 会触发 currentItemChanged，把选中状态清掉

    QListWidgetItem* toSelect = nullptr;

    for (const WorkspaceItem& workspace : snapshot.workspaces) {
        if (!m_showAll && workspace.id != snapshot.currentWorkspaceId) {
            continue;
        }

        QVector<const WindowItem*> group;
        for (const WindowItem& window : snapshot.windows) {
            if (window.workspaceId == workspace.id) {
                group.push_back(&window);
            }
        }
        if (group.isEmpty()) {
            continue;  // 空分组不画表头，避免一堆空标题
        }

        if (m_showAll) {
            auto* header = new QListWidgetItem(
                QStringLiteral("%1 · %2 个窗口").arg(workspace.name).arg(group.size()));
            // NoItemFlags：表头不可选中、不参与方向键移动，只是视觉分隔。
            header->setFlags(Qt::NoItemFlags);
            QFont font = m_windowList->font();
            font.setBold(true);
            header->setFont(font);
            header->setForeground(QColor(QStringLiteral("#86868b")));
            m_windowList->addItem(header);
        }

        for (const WindowItem* window : group) {
            QString text =
                window->title.isEmpty() ? QStringLiteral("（无标题窗口）") : window->title;
            text += QStringLiteral("\n") +
                    describe(*window, m_showAll ? workspace.name : QString());
            if (!window->failureMessage.isEmpty()) {
                text += QStringLiteral("\n! ") + window->failureMessage;
            }

            auto* item = new QListWidgetItem(text);
            item->setData(kWindowIdRole, QVariant::fromValue<qulonglong>(window->id));
            m_windowList->addItem(item);
            if (window->id == previouslySelected) {
                toSelect = item;
            }
        }
    }

    if (m_windowList->count() == 0) {
        auto* empty = new QListWidgetItem(
            m_showAll ? QStringLiteral("所有工作区都没有窗口")
                      : QStringLiteral("这个工作区还没有窗口，可从别的页移进来"));
        empty->setFlags(Qt::NoItemFlags);
        m_windowList->addItem(empty);
    }

    updateListCaption();

    if (toSelect) {
        m_windowList->setCurrentItem(toSelect);
        return;
    }

    if (previouslySelected == 0) {
        return;
    }

    // 之前选中的窗口不在当前显示的列表里了：先判断它是被挪走了还是被关掉了。
    // 这两种情况必须给不同的提示，否则用户会以为窗口丢了。
    m_hasSelection = false;
    m_selectedWindow = 0;
    updateSelectionUi();

    if (m_suppressSelectionNotice) {
        return;  // 是模式切换导致的，不是窗口真的变了
    }

    const WindowItem* still = findWindow(snapshot, previouslySelected);
    if (!still) {
        updateStatus(QStringLiteral("选中的窗口已经关闭，选择已清除。"), true);
        return;
    }
    updateStatus(
        QStringLiteral("窗口已移到「%1」。").arg(workspaceName(snapshot, still->workspaceId)));
}

void OverviewWindow::updateListCaption() {
    const OverviewSnapshot& snapshot = m_lastSnapshot;
    if (m_showAll) {
        m_listCaption->setText(
            QStringLiteral("所有窗口 · %1 个 · 按工作区分组").arg(snapshot.windows.size()));
        return;
    }
    int count = 0;
    for (const WindowItem& window : snapshot.windows) {
        if (window.workspaceId == snapshot.currentWorkspaceId) {
            ++count;
        }
    }
    m_listCaption->setText(QStringLiteral("%1 · %2 个窗口")
                               .arg(workspaceName(snapshot, snapshot.currentWorkspaceId))
                               .arg(count));
}

void OverviewWindow::onSelectionChanged(QListWidgetItem* current, QListWidgetItem*) {
    const QVariant data = current ? current->data(kWindowIdRole) : QVariant();
    if (!data.isValid()) {
        m_hasSelection = false;
        m_selectedWindow = 0;
    } else {
        m_hasSelection = true;
        m_selectedWindow = static_cast<WindowId>(data.toULongLong());
    }
    updateSelectionUi();
}

void OverviewWindow::onListActivated(QListWidgetItem* item) {
    if (!item) {
        return;
    }
    const QVariant data = item->data(kWindowIdRole);
    if (!data.isValid()) {
        return;  // 空列表占位项或分组表头
    }
    m_service->activateWindow(static_cast<WindowId>(data.toULongLong()));
}

void OverviewWindow::onMoveRequested() {
    if (!m_hasSelection) {
        updateStatus(QStringLiteral("先选中一个窗口，再移动。"), true);
        return;
    }

    // 目标列表要排除"这个窗口自己所在的工作区"，而不是"当前显示的工作区"。
    // 在"查看所有窗口"模式下这两者可能不是同一个。
    WorkspaceId ownWorkspace;
    if (const WindowItem* window = findWindow(m_lastSnapshot, m_selectedWindow)) {
        ownWorkspace = window->workspaceId;
    }

    QMenu menu(this);
    for (const WorkspaceItem& workspace : m_lastSnapshot.workspaces) {
        if (workspace.id == ownWorkspace) {
            continue;
        }
        QAction* action = menu.addAction(QStringLiteral("移动到「%1」").arg(workspace.name));
        action->setData(workspace.id);
    }

    if (menu.isEmpty()) {
        updateStatus(QStringLiteral("只有一个工作区，没有可移动的目标。"), true);
        return;
    }

    const QPoint anchor = m_moveButton->mapToGlobal(QPoint(0, m_moveButton->height()));
    QAction* chosen = menu.exec(anchor);
    if (!chosen) {
        return;
    }
    m_service->moveWindowToWorkspace(m_selectedWindow, chosen->data().toString());
}

void OverviewWindow::onToggleShowAll(bool checked) {
    m_showAll = checked;

    // 切换模式不是窗口状态变化，别把"选中项不在这张列表里"说成"窗口被移走"。
    m_suppressSelectionNotice = true;
    rebuildWindowList(m_lastSnapshot);
    m_suppressSelectionNotice = false;

    updateStatus(checked ? QStringLiteral("正在显示所有工作区的窗口。")
                         : QStringLiteral("正在只显示当前工作区的窗口。"));
}

void OverviewWindow::updateSelectionUi() {
    m_moveButton->setEnabled(m_hasSelection);
}

void OverviewWindow::onOperationFinished(WindowId, bool success, const QString& message) {
    updateStatus(message, !success);
}

void OverviewWindow::updateStatus(const QString& text, bool isError) {
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color:#c1121f;")
                                          : QStringLiteral("color:#6e6e73;"));
}

QString OverviewWindow::workspaceName(const OverviewSnapshot& snapshot, const WorkspaceId& id) const {
    for (const WorkspaceItem& workspace : snapshot.workspaces) {
        if (workspace.id == id) {
            return workspace.name;
        }
    }
    return QStringLiteral("（未知工作区）");
}

const WindowItem* OverviewWindow::findWindow(const OverviewSnapshot& snapshot, WindowId id) const {
    for (const WindowItem& window : snapshot.windows) {
        if (window.id == id) {
            return &window;
        }
    }
    return nullptr;
}

}  // namespace crossdesk::ui
