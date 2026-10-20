#include "main_window.h"

#include "layout_math.h"
#include "preview_widgets.h"
#include "window_row_delegate.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QWidget>

namespace crossdesk::ui {

namespace {

constexpr int kWindowIdRole = Qt::UserRole + 1;

// 左边任务栏的宽度。它是一个"条"，不该跟着屏幕无限变宽；
// 但也不能在 1366 的笔记本屏上占掉三分之一，所以按屏幕宽度取比例再夹住。
constexpr int kListMinWidth = 288;
constexpr int kListMaxWidth = 400;

// 行高的可用范围：太少看不清缩略图，太多一屏放不下几个窗口。
// 上限给得比较宽，是为了让"只有三个窗口"的时候把整条任务栏铺满，
// 而不是上面三行、下面一大片空。
constexpr int kRowMinHeight = 76;
constexpr int kRowMaxHeight = 260;

}  // namespace

MainWindow::MainWindow(CrossDeskService* service, QWidget* parent)
    : QWidget(parent), m_service(service) {
    buildUi();
    connectService();
    // 先扫一次：真实实现不主动扫描的话，启动时列表会是空的（看着像坏了）。
    m_service->refresh();
    applyWindows();  // 兼容异步实现：先把当前快照画出来
    setStatus(QStringLiteral("已载入 %1 个窗口。").arg(m_windows.size()));
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("CrossDesk"));
    setObjectName(QStringLiteral("MainWindow"));
    // 无边框 + 全屏：这块画面就是桌面本身，不跟用户抢标题栏和边框。
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setMinimumSize(960, 600);

    setStyleSheet(QStringLiteral(
        "QWidget#MainWindow { background:#0f1114; }"
        "QWidget { color:#e6e8ec; font-size:13px; }"
        "QListWidget#windowList { background:#1a1c21; border:none;"
        "  border-right:1px solid #262a31; outline:none; }"
        "QListWidget#windowList::item { border:none; padding:0; margin:0; }"
        "QPushButton { background:rgba(255,255,255,0.10); color:#f2f4f7;"
        "  border:1px solid rgba(255,255,255,0.14); border-radius:9px;"
        "  padding:7px 16px; font-size:13px; }"
        "QPushButton:hover { background:rgba(255,255,255,0.17); }"
        "QPushButton:pressed { background:rgba(255,255,255,0.24); }"
        "QPushButton:disabled { color:#6b7078; background:rgba(255,255,255,0.04);"
        "  border-color:rgba(255,255,255,0.06); }"
        "QPushButton#confirmButton { background:#0a84ff; border-color:#0a84ff;"
        "  color:#ffffff; font-weight:600; }"
        "QPushButton#confirmButton:hover { background:#3d9bff; border-color:#3d9bff; }"
        "QRadioButton { background:rgba(255,255,255,0.08); border-radius:8px;"
        "  padding:6px 12px; color:#c9ced6; }"
        "QRadioButton::indicator { image:none; width:0px; height:0px; margin:0px; }"
        "QRadioButton:checked { background:rgba(255,255,255,0.22); color:#ffffff; }"
        "QLabel#statusLabel { background:rgba(16,18,22,0.78);"
        "  border:1px solid rgba(255,255,255,0.10); border-radius:9px;"
        "  padding:5px 12px; color:#b9bfc8; font-size:12px; }"));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左：任务栏 ----
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("windowList"));
    m_list->setFocusPolicy(Qt::StrongFocus);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegate(new WindowRowDelegate(m_list));
    m_list->setUniformItemSizes(false);
    m_list->setFixedWidth(kListMaxWidth);
    connect(m_list, &QListWidget::itemChanged, this, &MainWindow::onItemChanged);
    // 回车 / 双击都落到同一条"定位"路径上。
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { onActivate(); });
    root->addWidget(m_list, 0);

    // ---- 右：主屏幕画面 ----
    m_canvas = new LayoutPreview(this);
    m_canvas->setObjectName(QStringLiteral("screenPreview"));
    // 换桌面 / 换排列都靠这个透明度效果做过渡，两段动画里的第二段就落在这里。
    m_sceneEffect = new QGraphicsOpacityEffect(m_canvas);
    m_sceneEffect->setOpacity(1.0);
    m_canvas->setGraphicsEffect(m_sceneEffect);
    root->addWidget(m_canvas, 1);

    // ---- 浮层：操作条 + 状态 ----
    m_actionBar = new QWidget(this);
    m_actionBar->setObjectName(QStringLiteral("actionBar"));
    auto* bar = new QHBoxLayout(m_actionBar);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(8);

    m_activateButton = new QPushButton(QStringLiteral("定位"), m_actionBar);
    m_activateButton->setObjectName(QStringLiteral("activateButton"));
    m_activateButton->setToolTip(QStringLiteral("Enter"));
    connect(m_activateButton, &QPushButton::clicked, this, &MainWindow::onActivate);
    bar->addWidget(m_activateButton);

    m_equalRadio = new QRadioButton(QStringLiteral("等宽"), m_actionBar);
    m_equalRadio->setObjectName(QStringLiteral("modeEqual"));
    m_equalRadio->setChecked(true);
    m_equalRadio->setToolTip(QStringLiteral("两窗各占一半"));
    m_masterRadio = new QRadioButton(QStringLiteral("左 60 / 右 40"), m_actionBar);
    m_masterRadio->setObjectName(QStringLiteral("modeLeftMaster"));
    m_masterRadio->setToolTip(QStringLiteral("左边主窗口更宽"));

    auto* group = new QButtonGroup(m_actionBar);
    group->addButton(m_equalRadio);
    group->addButton(m_masterRadio);
    connect(m_equalRadio, &QRadioButton::toggled, this, &MainWindow::onModeChanged);
    bar->addWidget(m_equalRadio);
    bar->addWidget(m_masterRadio);

    m_backButton = new QPushButton(QStringLiteral("返回"), m_actionBar);
    m_backButton->setObjectName(QStringLiteral("backButton"));
    m_backButton->setToolTip(QStringLiteral("Esc"));
    connect(m_backButton, &QPushButton::clicked, this, &MainWindow::onBack);
    bar->addWidget(m_backButton);

    m_confirmButton = new QPushButton(QStringLiteral("确认排列"), m_actionBar);
    m_confirmButton->setObjectName(QStringLiteral("confirmButton"));
    m_confirmButton->setToolTip(QStringLiteral("Ctrl+Enter"));
    connect(m_confirmButton, &QPushButton::clicked, this, &MainWindow::onConfirm);
    bar->addWidget(m_confirmButton);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setWordWrap(false);

    // 应用内快捷键。注意 Enter 没有单独注册：QListWidget 自己会把回车解释成
    // "激活当前项"，再注册一个 QShortcut 会和它抢同一个按键，出现一次回车发两条命令。
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), this, [this] { onRefresh(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this, [this] { onConfirm(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), this, [this] { onConfirm(); });

    updateActionStates();
}

void MainWindow::connectService() {
    connect(m_service, &CrossDeskService::windowsChanged, this, &MainWindow::applyWindows);
    connect(m_service, &CrossDeskService::operationFinished, this, &MainWindow::onOperationFinished);
}

void MainWindow::setAllowHide(bool allow) {
    m_allowHide = allow;
}

void MainWindow::applyWindows() {
    m_windows = m_service->windows();
    m_statusTakenByRefresh = false;

    // 已勾选但这次读不到了：说明窗口被关掉。必须从选择里摘掉，
    // 否则"确认排列"会把命令发给一个已经不存在的窗口。
    QVector<WindowId> kept;
    for (WindowId id : m_checked) {
        if (findWindow(id)) {
            kept.push_back(id);
        }
    }
    const bool lostSelection = kept.size() != m_checked.size();
    m_checked = kept;

    for (auto it = m_failures.begin(); it != m_failures.end();) {
        if (findWindow(it.key())) {
            ++it;
        } else {
            it = m_failures.erase(it);
        }
    }

    // 正在排列预览时，如果其中一个窗口被关掉，就没得排了，退回桌面视图。
    if (m_arranging && m_checked.size() < 2) {
        m_arranging = false;
        m_placements.clear();
    }

    rebuildList();
    updateActionStates();
    updateScene();

    if (lostSelection) {
        // 这条比"已刷新，共 N 个窗口"重要得多，所以用标记保护它不被覆盖。
        m_statusTakenByRefresh = true;
        setStatus(QStringLiteral("有已勾选的窗口已经关闭，已从选择中移除。"), true);
    }
}

QString MainWindow::textFor(const WindowItem& window) const {
    QString text = window.title.isEmpty() ? QStringLiteral("（无标题窗口）") : window.title;
    text += QLatin1Char('\n') + window.processName;
    if (const auto it = m_failures.constFind(window.id); it != m_failures.constEnd()) {
        text += QStringLiteral("\n! ") + it.value();
    }
    return text;
}

void MainWindow::rebuildList() {
    m_syncingChecks = true;
    m_list->clear();

    for (const WindowItem& window : m_windows) {
        auto* item = new QListWidgetItem(m_list);
        item->setText(textFor(window));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setData(kWindowIdRole,
                      QVariant::fromValue<qulonglong>(static_cast<qulonglong>(window.id)));
        item->setCheckState(m_checked.contains(window.id) ? Qt::Checked : Qt::Unchecked);
    }

    if (m_windows.isEmpty()) {
        auto* empty = new QListWidgetItem(m_list);
        empty->setText(QStringLiteral("没有可管理的窗口"));
        empty->setFlags(Qt::NoItemFlags);
        empty->setForeground(QColor(QStringLiteral("#6b7078")));
        empty->setSizeHint(QSize(m_list->viewport()->width(), 48));
    }

    m_syncingChecks = false;
    fitRows();
}

void MainWindow::fitRows() {
    // 让窗口少的时候行高一点、缩略图大一点，别让任务栏上面三行、下面全空。
    const int viewportHeight = m_list->viewport()->height();
    if (viewportHeight <= 0) {
        return;
    }

    int count = 0;
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(kWindowIdRole).isValid()) {
            ++count;
        }
    }
    if (count == 0) {
        return;
    }

    const int height = qBound(kRowMinHeight, viewportHeight / count, kRowMaxHeight);
    const int width = m_list->viewport()->width();
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem* item = m_list->item(row);
        if (item->data(kWindowIdRole).isValid()) {
            item->setSizeHint(QSize(width, height));
        }
    }
}

void MainWindow::refreshRow(WindowId id) {
    const WindowItem* window = findWindow(id);
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem* item = m_list->item(row);
        if (!item || item->data(kWindowIdRole).toULongLong() != static_cast<qulonglong>(id)) {
            continue;
        }
        m_syncingChecks = true;
        item->setText(window ? textFor(*window) : item->text());
        m_syncingChecks = false;
        return;
    }
}

void MainWindow::onItemChanged(QListWidgetItem* item) {
    if (m_syncingChecks || !item) {
        return;
    }
    const QVariant data = item->data(kWindowIdRole);
    if (!data.isValid()) {
        return;
    }
    const WindowId id = static_cast<WindowId>(data.toULongLong());

    if (item->checkState() == Qt::Checked) {
        if (m_checked.contains(id)) {
            return;
        }
        if (m_checked.size() >= 2) {
            // 最多两个：勾第三个时替换最早的，并在状态栏说清楚，
            // 不做"点了没反应"这种让人怀疑程序坏了的行为。
            const WindowId dropped = m_checked.takeFirst();
            m_syncingChecks = true;
            for (int row = 0; row < m_list->count(); ++row) {
                QListWidgetItem* other = m_list->item(row);
                if (other && other->data(kWindowIdRole).toULongLong() ==
                                 static_cast<qulonglong>(dropped)) {
                    other->setCheckState(Qt::Unchecked);
                }
            }
            m_syncingChecks = false;
            setStatus(QStringLiteral("最多同时排列两个窗口，已替换掉最早勾选的那个。"));
        }
        m_checked.push_back(id);
    } else {
        m_checked.removeAll(id);
        if (m_arranging) {
            // 在排列画面里取消勾选：没有两个窗口就没得排，直接退回桌面。
            m_arranging = false;
            m_placements.clear();
        }
    }

    updateActionStates();
    if (m_arranging) {
        // 已经在排列画面里：勾选变了（比如勾第三个把最早的替换掉），
        // 直接把画面换成新的一对，不做淡入淡出——用户刚点的就是这件事。
        if (m_checked.size() == 2 && buildPlacements()) {
            updateScene();
        }
        return;
    }
    updateScene();

    // 勾满两个窗口就自动把画面切到"排完之后的样子"——用户不用先找按钮，
    // 勾完就看见结果，这正是这个工具要说的话。
    if (m_checked.size() == 2) {
        enterArrangement();
    }
}

void MainWindow::updateActionStates() {
    const int count = m_checked.size();
    const bool arranging = m_arranging;
    const bool showActivate = !arranging && count == 1;

    m_activateButton->setVisible(showActivate);
    m_activateButton->setEnabled(showActivate && !m_animating);

    m_equalRadio->setVisible(arranging);
    m_masterRadio->setVisible(arranging);
    m_backButton->setVisible(arranging);
    m_backButton->setEnabled(!m_animating);
    m_confirmButton->setVisible(arranging);
    m_confirmButton->setEnabled(arranging && m_placements.size() == 2 && !m_animating);

    // 操作条的显隐要由"该显示什么"直接算出来，不能拿子控件的 isVisible():
    // 父控件自己隐藏着的时候，子控件的 isVisible() 一律返回 false。
    m_actionBar->setVisible(showActivate || arranging);
    m_actionBar->adjustSize();
    layoutOverlay();
}

void MainWindow::updateScene() {
    // 右侧画面只有两种内容：桌面现在的样子 / 排完之后的样子。
    // 都由 LayoutPreview 画，界面这一层不重复算位置。
    if (m_arranging && m_placements.size() == 2) {
        m_canvas->showArrangement(m_service->workArea(), m_placements, m_windows);
        return;
    }
    m_canvas->showDesktop(m_service->workArea(), m_windows, m_checked);
}

void MainWindow::layoutOverlay() {
    if (!m_actionBar) {
        return;
    }
    const QRect area = m_canvas->geometry();
    const QSize barSize = m_actionBar->sizeHint();
    m_actionBar->setGeometry(area.right() - barSize.width() - 28,
                             area.bottom() - barSize.height() - 24, barSize.width(),
                             barSize.height());
    m_actionBar->raise();

    // 状态条贴着左下角，宽度不超过画面的一半，长了就省略。
    const QSize statusSize = m_statusLabel->sizeHint();
    const int statusWidth = qMin(statusSize.width(), qMax(240, area.width() / 2));
    m_statusLabel->setGeometry(area.left() + 24, area.bottom() - 24 - 30, statusWidth, 30);
    m_statusLabel->raise();
}

void MainWindow::onRefresh() {
    if (m_animating) {
        return;
    }
    m_service->refresh();
    // refresh 会同步发出 windowsChanged；如果那边留下了更重要的提示（窗口已关闭），
    // 这里就不要用"已刷新"把它冲掉。
    if (!m_statusTakenByRefresh) {
        setStatus(QStringLiteral("已刷新，共 %1 个窗口。").arg(m_windows.size()));
    }
}

void MainWindow::onActivate() {
    if (m_animating) {
        return;
    }
    if (m_checked.size() != 1) {
        setStatus(m_checked.isEmpty()
                      ? QStringLiteral("先在左侧勾选一个窗口，再定位。")
                      : QStringLiteral("勾选了多个窗口，定位时请只留一个。"),
                  true);
        return;
    }
    m_service->activateWindow(m_checked.first());
}

void MainWindow::enterArrangement() {
    if (!buildPlacements()) {
        return;  // 失败原因已写进状态栏，留在桌面视图不动
    }
    m_arranging = true;
    updateActionStates();
    fadeScene([this] { updateScene(); });
}

void MainWindow::leaveArrangement() {
    if (!m_arranging) {
        return;
    }
    m_arranging = false;
    m_placements.clear();
    updateActionStates();
    fadeScene([this] { updateScene(); });
}

void MainWindow::onBack() {
    if (m_animating) {
        return;
    }
    leaveArrangement();
}

bool MainWindow::buildPlacements() {
    QVector<QRect> rects;
    QString error;
    if (!computeLayout(m_service->workArea(), m_checked.size(), currentMode(), kPaneGap, rects,
                       &error)) {
        setStatus(error, true);
        m_placements.clear();
        return false;
    }

    m_placements.clear();
    for (int i = 0; i < rects.size(); ++i) {
        m_placements.push_back(Placement{m_checked.at(i), rects.at(i)});
    }
    return true;
}

LayoutMode MainWindow::currentMode() const {
    return m_masterRadio->isChecked() ? LayoutMode::LeftMaster : LayoutMode::Equal;
}

void MainWindow::onModeChanged() {
    if (!m_arranging || m_checked.size() != 2) {
        return;
    }
    // 换比例立刻重画，不用退出再进来。这里不淡入淡出：切比例要的就是"马上看到"。
    if (buildPlacements()) {
        updateScene();
    }
    updateActionStates();
}

void MainWindow::onConfirm() {
    if (m_animating) {
        return;
    }
    if (m_placements.size() != 2) {
        setStatus(QStringLiteral("先勾选两个窗口，等画面出现排列结果后再确认。"), true);
        return;
    }

    m_confirmPending = m_placements.size();
    m_confirmFailures = 0;
    setStatus(QStringLiteral("正在应用排列…"));

    for (const Placement& placement : m_placements) {
        m_service->placeWindow(placement.id, placement.rect);
    }

    // 同步实现会在这里已经把结果回调完了。收尾统一放到事件循环的下一轮，
    // 避免"正在遍历 m_placements 时它被清空"这种自己咬自己的问题。
    if (m_confirmPending == 0) {
        QTimer::singleShot(0, this, [this] { finishConfirm(); });
    }
}

void MainWindow::finishConfirm() {
    m_statusTakenByRefresh = false;
    m_service->refresh();  // M3：排列后刷新就能看到最新位置

    if (!m_statusTakenByRefresh) {
        if (m_confirmFailures == 0) {
            setStatus(QStringLiteral("排列完成。"));
        } else {
            setStatus(QStringLiteral("排列未全部成功：%1 个窗口失败，原因见左侧列表里的红字。")
                          .arg(m_confirmFailures),
                      true);
        }
    }

    // 回到桌面视图：这时候两个窗口的真实位置已经变成左右两栏，
    // 画面上直接就能看到结果，不需要再额外来一句"成功"。
    m_arranging = false;
    m_placements.clear();
    updateActionStates();
    fadeScene([this] { updateScene(); });
}

void MainWindow::onOperationFinished(const OperationResult& result) {
    if (result.window != 0) {
        if (result.success) {
            m_failures.remove(result.window);
        } else {
            m_failures.insert(result.window, result.message);
        }
    }

    if (m_confirmPending > 0) {
        --m_confirmPending;
        if (!result.success) {
            ++m_confirmFailures;
        }
        if (m_confirmPending == 0) {
            QTimer::singleShot(0, this, [this] { finishConfirm(); });
        }
        return;
    }

    // 单步命令（定位）：先把结果写进状态栏，再只改这一行的文字。
    // 不整表重建，因为这条路径可能是从列表自己的回车处理里发出来的，
    // 在列表内部处理键盘事件时清空列表是自找崩溃。
    setStatus(result.message, !result.success);
    if (result.window != 0) {
        refreshRow(result.window);
    }
}

void MainWindow::fadeScene(const std::function<void()>& swap) {
    if (m_animating) {
        swap();
        return;
    }

    m_animating = true;
    updateActionStates();  // 动画期间把按钮禁掉：连点两下不能发两条命令

    auto* out = new QPropertyAnimation(m_sceneEffect, "opacity", this);
    out->setDuration(110);
    out->setStartValue(1.0);
    out->setEndValue(0.0);
    out->setEasingCurve(QEasingCurve::InQuad);
    connect(out, &QPropertyAnimation::finished, this, [this, swap] {
        swap();  // 画面全透明的那一瞬间换数据，用户看不到"跳变"

        auto* in = new QPropertyAnimation(m_sceneEffect, "opacity", this);
        in->setDuration(170);
        in->setStartValue(0.0);
        in->setEndValue(1.0);
        in->setEasingCurve(QEasingCurve::OutCubic);
        connect(in, &QPropertyAnimation::finished, this, [this] {
            m_animating = false;
            updateActionStates();
        });
        in->start(QAbstractAnimation::DeleteWhenStopped);
    });
    out->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::collapse() {
    if (m_animating) {
        return;
    }
    if (m_arranging) {
        leaveArrangement();  // 排列预览先退回桌面，再按一次才收起
        return;
    }
    if (!isVisible()) {
        return;
    }

    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(140);
    fade->setStartValue(windowOpacity());
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::InCubic);
    connect(fade, &QPropertyAnimation::finished, this, [this] {
        setWindowOpacity(1.0);
        if (m_allowHide) {
            hide();  // 全局热键已接入，可以用 Ctrl+Alt+M 叫回来
        } else {
            // 没有全局热键时隐藏等于"窗口消失且无法找回"，所以退到任务栏。
            // 这是交接文档要求的那条回退路径，不是临时凑合。
            showMinimized();
        }
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_arranging) {
            leaveArrangement();
        } else {
            collapse();
        }
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!m_firstShow) {
        setWindowOpacity(1.0);
        return;
    }

    // 第一次出现时淡入：M4 说的"打开总览有过渡"就落在这里。
    m_firstShow = false;
    setWindowOpacity(0.0);
    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(180);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // 任务栏宽度按屏幕宽度取 22%。固定值在 1366 的笔记本屏上会占掉三分之一，
    // 在 4K 上又窄得像根线。
    const int listWidth = qBound(kListMinWidth, width() * 22 / 100, kListMaxWidth);
    if (m_list->width() != listWidth) {
        m_list->setFixedWidth(listWidth);
    }

    fitRows();
    layoutOverlay();
}

void MainWindow::setStatus(const QString& text, bool error) {
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color:#ff6b60;")
                                       : QStringLiteral("color:#b9bfc8;"));
    m_statusLabel->setVisible(!text.isEmpty());
    layoutOverlay();
}

const WindowItem* MainWindow::findWindow(WindowId id) const {
    for (const WindowItem& window : m_windows) {
        if (window.id == id) {
            return &window;
        }
    }
    return nullptr;
}

}  // namespace crossdesk::ui
