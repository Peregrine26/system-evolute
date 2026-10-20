#include "main_window.h"

#include "layout_math.h"
#include "preview_widgets.h"

#include <QButtonGroup>
#include <QColor>
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
#include <QShortcut>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

namespace crossdesk::ui {

namespace {

constexpr int kWindowIdRole = Qt::UserRole + 1;
constexpr int kOverviewPage = 0;
constexpr int kPreviewPage = 1;

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
    resize(1080, 700);

    setStyleSheet(QStringLiteral(
        "QWidget { background:#f5f5f7; color:#1d1d1f; font-size:13px; }"
        "QLabel[role=\"title\"] { font-size:19px; font-weight:700; }"
        "QLabel[role=\"hint\"] { color:#86868b; }"
        "QPushButton { background:#ffffff; border:1px solid #d2d2d7; border-radius:8px;"
        "  padding:6px 16px; }"
        "QPushButton:hover { border-color:#a1a1a6; }"
        "QPushButton:disabled { color:#b0b0b5; border-color:#e3e3e8; }"
        "QListWidget { background:#ffffff; border:1px solid #d2d2d7; border-radius:12px;"
        "  outline:none; }"
        "QListWidget::item { padding:8px 10px; border-bottom:1px solid #f0f0f2; }"
        "QListWidget::item:selected { background:#e8f1fd; color:#1d1d1f; }"
        "QRadioButton { background:transparent; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 14);
    root->setSpacing(12);

    auto* header = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("CrossDesk"), this);
    title->setProperty("role", "title");
    auto* hint = new QLabel(QStringLiteral("窗口总览 · 定位或左右排列"), this);
    hint->setProperty("role", "hint");
    header->addWidget(title);
    header->addSpacing(10);
    header->addWidget(hint);
    header->addStretch(1);
    root->addLayout(header);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("pages"));
    m_pages->addWidget(buildOverviewPage());
    m_pages->addWidget(buildPreviewPage());

    // 换页动画靠这个透明度效果，两个页面共用同一个，避免切换时闪烁。
    m_pageEffect = new QGraphicsOpacityEffect(m_pages);
    m_pageEffect->setOpacity(1.0);
    m_pages->setGraphicsEffect(m_pageEffect);

    root->addWidget(m_pages, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMinimumHeight(20);
    root->addWidget(m_statusLabel);

    // 应用内快捷键。注意 Enter 没有单独注册：QListWidget 自己会把回车解释成
    // "激活当前项"，再注册一个 QShortcut 会和它抢同一个按键，出现一次回车发两条命令。
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), this, [this] { onRefresh(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this, [this] { onConfirm(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), this, [this] { onConfirm(); });
}

QWidget* MainWindow::buildOverviewPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* bar = new QHBoxLayout();
    m_refreshButton = new QPushButton(QStringLiteral("刷新"), page);
    m_refreshButton->setObjectName(QStringLiteral("refreshButton"));
    m_refreshButton->setToolTip(QStringLiteral("Ctrl+R"));
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::onRefresh);
    bar->addWidget(m_refreshButton);

    m_activateButton = new QPushButton(QStringLiteral("定位"), page);
    m_activateButton->setObjectName(QStringLiteral("activateButton"));
    m_activateButton->setToolTip(QStringLiteral("勾选一个窗口后按 Enter"));
    connect(m_activateButton, &QPushButton::clicked, this, &MainWindow::onActivate);
    bar->addWidget(m_activateButton);

    m_arrangeButton = new QPushButton(QStringLiteral("排列…"), page);
    m_arrangeButton->setObjectName(QStringLiteral("arrangeButton"));
    m_arrangeButton->setToolTip(QStringLiteral("勾选两个窗口后进入布局预览"));
    connect(m_arrangeButton, &QPushButton::clicked, this, &MainWindow::onArrange);
    bar->addWidget(m_arrangeButton);

    bar->addStretch(1);
    auto* counter = new QLabel(QStringLiteral("勾选最多两个窗口"), page);
    counter->setProperty("role", "hint");
    bar->addWidget(counter);
    layout->addLayout(bar);

    auto* body = new QHBoxLayout();
    body->setSpacing(14);

    m_list = new QListWidget(page);
    m_list->setObjectName(QStringLiteral("windowList"));
    m_list->setFocusPolicy(Qt::StrongFocus);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list, &QListWidget::itemChanged, this, &MainWindow::onItemChanged);
    // 回车 / 双击都落到同一条"定位"路径上，和按钮共用命令。
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { onActivate(); });
    body->addWidget(m_list, 3);

    auto* previewColumn = new QVBoxLayout();
    previewColumn->setSpacing(8);
    auto* caption = new QLabel(QStringLiteral("实时预览"), page);
    caption->setProperty("role", "hint");
    previewColumn->addWidget(caption);
    m_slotA = new ThumbnailSlot(page);
    m_slotA->setObjectName(QStringLiteral("thumbnailA"));
    m_slotB = new ThumbnailSlot(page);
    m_slotB->setObjectName(QStringLiteral("thumbnailB"));
    previewColumn->addWidget(m_slotA, 1);
    previewColumn->addWidget(m_slotB, 1);
    body->addLayout(previewColumn, 2);

    layout->addLayout(body, 1);
    return page;
}

QWidget* MainWindow::buildPreviewPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* bar = new QHBoxLayout();
    m_backButton = new QPushButton(QStringLiteral("返回"), page);
    m_backButton->setObjectName(QStringLiteral("backButton"));
    m_backButton->setToolTip(QStringLiteral("Esc"));
    connect(m_backButton, &QPushButton::clicked, this, &MainWindow::onBack);
    bar->addWidget(m_backButton);
    bar->addStretch(1);

    m_equalRadio = new QRadioButton(QStringLiteral("等宽"), page);
    m_equalRadio->setObjectName(QStringLiteral("modeEqual"));
    m_equalRadio->setChecked(true);
    m_masterRadio = new QRadioButton(QStringLiteral("左侧主窗口 60/40"), page);
    m_masterRadio->setObjectName(QStringLiteral("modeLeftMaster"));

    auto* group = new QButtonGroup(page);
    group->addButton(m_equalRadio);
    group->addButton(m_masterRadio);
    connect(m_equalRadio, &QRadioButton::toggled, this, &MainWindow::onModeChanged);

    bar->addWidget(m_equalRadio);
    bar->addSpacing(12);
    bar->addWidget(m_masterRadio);
    bar->addSpacing(16);

    m_confirmButton = new QPushButton(QStringLiteral("确认排列"), page);
    m_confirmButton->setObjectName(QStringLiteral("confirmButton"));
    m_confirmButton->setToolTip(QStringLiteral("Ctrl+Enter"));
    connect(m_confirmButton, &QPushButton::clicked, this, &MainWindow::onConfirm);
    bar->addWidget(m_confirmButton);

    layout->addLayout(bar);

    m_preview = new LayoutPreview(page);
    m_preview->setObjectName(QStringLiteral("previewCanvas"));
    layout->addWidget(m_preview, 1);
    return page;
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

    rebuildList();
    updateActionStates();
    updateThumbnails();

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
        empty->setText(QStringLiteral(
            "没有可管理的窗口。\n点「刷新」重试；若一直为空，说明平台扫描还没就绪。"));
        empty->setFlags(Qt::NoItemFlags);
        empty->setForeground(QColor(QStringLiteral("#86868b")));
    }

    m_syncingChecks = false;
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
    }

    updateActionStates();
    updateThumbnails();
}

void MainWindow::updateActionStates() {
    const int count = m_checked.size();
    m_activateButton->setEnabled(count == 1);
    m_arrangeButton->setEnabled(count == 2);
    m_confirmButton->setEnabled(count == 2 && m_placements.size() == 2);
}

void MainWindow::updateThumbnails() {
    // 两个预览位优先显示勾选的窗口；勾不满就按列表顺序补齐，
    // 这样"总览里同时有两个实时预览"在没勾选时也成立。
    QVector<const WindowItem*> shown;
    const auto add = [&shown](const WindowItem* window) {
        if (!window || shown.size() >= 2) {
            return;
        }
        for (const WindowItem* existing : shown) {
            if (existing->id == window->id) {
                return;
            }
        }
        shown.push_back(window);
    };

    for (WindowId id : m_checked) {
        add(findWindow(id));
    }
    for (const WindowItem& window : m_windows) {
        add(&window);
    }

    m_slotA->setWindow(shown.size() > 0 ? shown.at(0) : nullptr);
    m_slotB->setWindow(shown.size() > 1 ? shown.at(1) : nullptr);
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
                      ? QStringLiteral("先在列表里勾选一个窗口，再定位。")
                      : QStringLiteral("勾选了多个窗口，定位时请只留一个。"),
                  true);
        return;
    }
    m_service->activateWindow(m_checked.first());
}

void MainWindow::onArrange() {
    if (m_animating) {
        return;
    }
    if (m_checked.size() != 2) {
        setStatus(QStringLiteral("勾选两个窗口后才能预览排列。"), true);
        return;
    }
    if (!buildPlacements()) {
        return;  // 失败原因已写进状态栏，留在总览页不动
    }
    updateActionStates();  // m_placements 变了，"确认排列"才可用
    fadeToPage(kPreviewPage);
}

bool MainWindow::buildPlacements() {
    QVector<QRect> rects;
    QString error;
    if (!computeLayout(m_service->workArea(), m_checked.size(), currentMode(), kPaneGap, rects,
                       &error)) {
        setStatus(error, true);
        return false;
    }

    m_placements.clear();
    for (int i = 0; i < rects.size(); ++i) {
        m_placements.push_back(Placement{m_checked.at(i), rects.at(i)});
    }
    m_preview->setPlacements(m_service->workArea(), m_placements, m_windows);
    return true;
}

LayoutMode MainWindow::currentMode() const {
    return m_masterRadio->isChecked() ? LayoutMode::LeftMaster : LayoutMode::Equal;
}

void MainWindow::onModeChanged() {
    if (m_pages->currentIndex() != kPreviewPage) {
        return;
    }
    if (m_checked.size() != 2) {
        return;
    }
    buildPlacements();  // 换比例立刻重画，不用退出再进来
}

void MainWindow::onConfirm() {
    if (m_animating) {
        return;
    }
    if (m_placements.size() != 2) {
        setStatus(QStringLiteral("先勾选两个窗口，进入排列预览后再确认。"), true);
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
            setStatus(QStringLiteral("排列未全部成功：%1 个窗口失败，原因见列表里的 ! 标记。")
                          .arg(m_confirmFailures),
                      true);
        }
    }
    fadeToPage(kOverviewPage);
}

void MainWindow::onBack() {
    if (m_animating) {
        return;
    }
    if (m_pages->currentIndex() == kPreviewPage) {
        fadeToPage(kOverviewPage);
    }
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

void MainWindow::fadeToPage(int index) {
    if (m_animating || m_pages->currentIndex() == index) {
        return;
    }

    m_animating = true;
    // 动画期间禁用会改变状态的按钮：连点两下不能发两条命令。
    m_refreshButton->setEnabled(false);
    m_activateButton->setEnabled(false);
    m_arrangeButton->setEnabled(false);
    m_confirmButton->setEnabled(false);

    auto* out = new QPropertyAnimation(m_pageEffect, "opacity", this);
    out->setDuration(110);
    out->setStartValue(1.0);
    out->setEndValue(0.0);
    out->setEasingCurve(QEasingCurve::InQuad);
    connect(out, &QPropertyAnimation::finished, this, [this, index] {
        m_pages->setCurrentIndex(index);

        auto* in = new QPropertyAnimation(m_pageEffect, "opacity", this);
        in->setDuration(150);
        in->setStartValue(0.0);
        in->setEndValue(1.0);
        in->setEasingCurve(QEasingCurve::OutQuad);
        connect(in, &QPropertyAnimation::finished, this, [this] {
            m_animating = false;
            m_refreshButton->setEnabled(true);
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
    if (m_pages->currentIndex() == kPreviewPage) {
        onBack();  // 预览态先退回总览，再按一次才收起
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
        if (m_pages->currentIndex() == kPreviewPage) {
            onBack();
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
    fade->setDuration(170);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::setStatus(const QString& text, bool error) {
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color:#c1121f;")
                                       : QStringLiteral("color:#6e6e73;"));
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
