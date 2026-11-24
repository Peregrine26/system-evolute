#include "desktop/workbench.h"
#include "desktop/pages.h"
#include "desktop/shell.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QDialogButtonBox>
#include <QEnterEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QKeySequenceEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

namespace crossdesk::desktop {
namespace {
QString screenKey(QScreen* screen) {
    const auto identity = screen->manufacturer() + '/' + screen->model() + '/' + screen->serialNumber();
    return screen->serialNumber().isEmpty() ? screen->name() : identity;
}
QRect visibleGeometry(const QJsonObject& config) {
    QScreen* screen = QGuiApplication::primaryScreen();
    for (auto* candidate : QGuiApplication::screens())
        if (screenKey(candidate) == config["screen"].toString()) screen = candidate;
    const QRect area = screen->availableGeometry();
    const int width = qMin(qMax(240, config["width"].toInt(360)), area.width());
    const int height = qMin(qMax(150, config["height"].toInt(320)), area.height());
    return {std::clamp(config["x"].toInt(area.x() + 40), area.left(), area.right() - width + 1),
            std::clamp(config["y"].toInt(area.y() + 40), area.top(), area.bottom() - height + 1), width, height};
}
}

class WidgetFrame final : public QWidget {
public:
    WidgetFrame(Store& store, const QStringList& ids, const std::function<void(const QString&)>& notify)
        : QWidget(nullptr, Qt::Tool), store_(store), ids_(ids) {
        setObjectName("desktopWidget");
        const auto config = store_.widget(ids_.first());
        setWindowTitle(ids_.size() > 1 ? QStringLiteral("格子组") : config["title"].toString());
        setMinimumSize(240, 150);
        auto* layout = new QVBoxLayout(this); layout->setContentsMargins(8, 8, 8, 8);
        auto* tools = new QHBoxLayout;
        auto* title = new QLabel(windowTitle()); title->setTextInteractionFlags(Qt::TextSelectableByMouse); tools->addWidget(title, 1);
        collapse_ = new QToolButton; collapse_->setText(QStringLiteral("收起")); tools->addWidget(collapse_);
        auto* pin = new QToolButton; pin->setText(QStringLiteral("置顶")); pin->setCheckable(true); pin->setChecked(config["pinned"].toBool()); tools->addWidget(pin);
        auto* rename = new QToolButton; rename->setText(QStringLiteral("名称")); tools->addWidget(rename); layout->addLayout(tools);
        auto* feedback = new QLabel(this); feedback->setWordWrap(true);
        const auto pageNotify = [feedback, notify](const QString& message) { feedback->setText(message); notify(message); };
        if (ids_.size() > 1) {
            auto* tabs = new QTabWidget; tabs->setMovable(true); body_ = tabs;
            for (const auto& id : ids_) tabs->addTab(createPage(store_, id, pageNotify), store_.widget(id)["title"].toString());
        } else body_ = createPage(store_, ids_.first(), pageNotify);
        layout->addWidget(body_, 1);
        layout->addWidget(feedback);
        connect(&store_, &Store::persistenceFailed, this, [feedback](const QString& error) { feedback->setText(error); });
        setGeometry(visibleGeometry(config));
        expandedHeight_ = height();
        setWindowFlag(Qt::WindowStaysOnTopHint, pin->isChecked());
        const auto opacity = store_.settings()["opacity"].toDouble(1.0); setWindowOpacity(std::clamp(opacity, 0.35, 1.0));
        collapsed_ = config["collapsed"].toBool(); applyCollapsed(false);
        connect(collapse_, &QToolButton::clicked, this, [this] {
            collapsed_ = !collapsed_; hoverExpanded_ = false; applyCollapsed(true); persist();
        });
        connect(pin, &QToolButton::toggled, this, [this](bool enabled) {
            setWindowFlag(Qt::WindowStaysOnTopHint, enabled); show();
            auto config = store_.widget(ids_.first()); config["pinned"] = enabled; store_.putWidget(config);
        });
        connect(rename, &QToolButton::clicked, this, [this, title] {
            bool ok = false; const auto text = QInputDialog::getText(this, QStringLiteral("格子名称"), QStringLiteral("名称"), QLineEdit::Normal, windowTitle(), &ok);
            if (!ok || text.trimmed().isEmpty()) return;
            setWindowTitle(text.trimmed()); title->setText(windowTitle());
            auto config = store_.widget(ids_.first()); config["title"] = windowTitle(); store_.putWidget(config);
        });
        saveTimer_.setSingleShot(true); saveTimer_.setInterval(250);
        connect(&saveTimer_, &QTimer::timeout, this, [this] { persist(); });
        ready_ = true;
    }
    ~WidgetFrame() override { ready_ = false; saveTimer_.stop(); persist(); }
    void setShuttingDown() { shuttingDown_ = true; }
    bool focusKind(const QString& kind, bool newNote = false) {
        for (int i = 0; i < ids_.size(); ++i) {
            if (store_.widget(ids_[i])["kind"].toString() != kind) continue;
            QWidget* page = body_;
            if (auto* tabs = qobject_cast<QTabWidget*>(body_)) { tabs->setCurrentIndex(i); page = tabs->widget(i); }
            collapsed_ = false; hoverExpanded_ = false; applyCollapsed(false);
            showNormal(); raise(); activateWindow();
            if (newNote) if (auto* button = page->findChild<QPushButton*>("newNote")) button->click();
            if (auto* input = page->findChild<QLineEdit*>("searchQuery")) input->setFocus();
            if (auto* editor = page->findChild<QWidget*>("noteEditor")) editor->setFocus();
            return true;
        }
        return false;
    }
    void updateOpacity(double opacity) { setWindowOpacity(std::clamp(opacity, 0.35, 1.0)); }
protected:
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); if (ready_) saveTimer_.start(); }
    void moveEvent(QMoveEvent* event) override { QWidget::moveEvent(event); if (ready_) saveTimer_.start(); }
    void closeEvent(QCloseEvent* event) override {
        persist();
        if (!shuttingDown_) {
            for (const auto& id : ids_) { auto config = store_.widget(id); config["hidden"] = true; store_.putWidget(config); }
        }
        QWidget::closeEvent(event);
    }
    void enterEvent(QEnterEvent* event) override {
        QWidget::enterEvent(event);
        if (collapsed_ && store_.settings()["hoverReveal"].toBool()) { hoverExpanded_ = true; applyCollapsed(true); }
    }
    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        if (hoverExpanded_) QTimer::singleShot(350, this, [this] {
            if (!underMouse() && !QApplication::activePopupWidget() && !QApplication::activeModalWidget()) {
                hoverExpanded_ = false; applyCollapsed(true);
            }
        });
    }
private:
    void applyCollapsed(bool animate) {
        const bool small = collapsed_ && !hoverExpanded_;
        if (small && body_->isVisible()) expandedHeight_ = height();
        body_->setVisible(!small); collapse_->setText(small ? QStringLiteral("展开") : QStringLiteral("收起"));
        setMinimumHeight(small ? 48 : 150);
        const QRect target(x(), y(), width(), small ? 52 : qMax(150, expandedHeight_));
        animation_.stop();
        if (animate) {
            animation_.setTargetObject(this); animation_.setPropertyName("geometry");
            animation_.setStartValue(geometry()); animation_.setEndValue(target);
            animation_.setDuration(150); animation_.setEasingCurve(QEasingCurve::OutCubic); animation_.start();
        } else setGeometry(target);
    }
    void persist() {
        if (ids_.isEmpty()) return;
        auto config = store_.widget(ids_.first()); if (config.isEmpty()) return;
        config["x"] = x(); config["y"] = y(); config["width"] = qMax(240, width());
        config["height"] = collapsed_ ? qMax(150, expandedHeight_) : qMax(150, height());
        config["collapsed"] = collapsed_;
        if (screen()) config["screen"] = screenKey(screen());
        store_.putWidget(config);
    }
    Store& store_; QStringList ids_; QWidget* body_; QToolButton* collapse_;
    QTimer saveTimer_; QPropertyAnimation animation_; bool collapsed_ = false;
    bool hoverExpanded_ = false; bool ready_ = false; bool shuttingDown_ = false; int expandedHeight_ = 320;
};

Workbench::Workbench(Store& store, QWidget* parent) : QMainWindow(parent), store_(store) {
    setWindowTitle(QStringLiteral("CrossDesk")); resize(920, 660); setMinimumSize(680, 480);
    auto* body = new QWidget; auto* layout = new QVBoxLayout(body); layout->setContentsMargins(28, 24, 28, 20); layout->setSpacing(16);
    auto* heading = new QLabel(QStringLiteral("让桌面各归其位")); heading->setStyleSheet("font-size:28px;font-weight:600;"); layout->addWidget(heading);
    summary_ = new QLabel; layout->addWidget(summary_);
    auto* createRow = new QHBoxLayout;
    for (const auto* kind : {"folder", "todo", "note", "clock", "weather", "music", "search"}) {
        auto* action = new QPushButton(kindTitle(kind)); createRow->addWidget(action);
        connect(action, &QPushButton::clicked, this, [this, kind] { create(kind); });
    }
    layout->addLayout(createRow);
    list_ = new QListWidget; list_->setObjectName("widgetList"); list_->setSelectionMode(QAbstractItemView::ExtendedSelection); layout->addWidget(list_, 1);
    auto* tools = new QHBoxLayout;
    auto addAction = [this, tools](const QString& title, const std::function<void()>& action) {
        auto* button = new QPushButton(title); tools->addWidget(button); connect(button, &QPushButton::clicked, this, action);
    };
    addAction(QStringLiteral("显示所选"), [this] { showSelected(); });
    addAction(QStringLiteral("合成标签组"), [this] { groupSelected(true); });
    addAction(QStringLiteral("拆分标签组"), [this] { groupSelected(false); });
    addAction(QStringLiteral("删除格子"), [this] {
        const auto ids = selectedIds(); if (ids.isEmpty()) return;
        if (QMessageBox::question(this, QStringLiteral("删除格子"), QStringLiteral("只移除格子，文件和待办随记仍保留。继续？")) != QMessageBox::Yes) return;
        for (const auto& id : ids) store_.removeWidget(id); rebuildFrames();
    });
    tools->addStretch(); addAction(QStringLiteral("窗口总览"), [this] { if (openWindowOverview) openWindowOverview(); else notify(QStringLiteral("当前构建未启用窗口总览")); });
    layout->addLayout(tools); shortcut_ = new QLabel; shortcut_->setWordWrap(true); layout->addWidget(shortcut_); setCentralWidget(body);
    auto* file = menuBar()->addMenu(QStringLiteral("桌面"));
    file->addAction(QStringLiteral("显示全部格子"), this, [this] {
        for (const auto& value : store_.widgets()) { auto config = value.toObject(); config["hidden"] = false; store_.putWidget(config); }
        rebuildFrames();
    });
    file->addAction(QStringLiteral("收起 / 显示所有格子"), this, [this] {
        bool anyVisible = false; for (const auto& frame : frames_) if (frame && frame->isVisible()) anyVisible = true;
        for (const auto& frame : frames_) if (frame) frame->setVisible(!anyVisible);
    });
    file->addAction(QStringLiteral("整理文件夹…"), this, [this] { organize(); });
    file->addAction(QStringLiteral("撤销上次文件移动"), this, [this] { undoOrganization(); });
    file->addSeparator(); file->addAction(QStringLiteral("退出"), this, [this] { quitting_ = true; qApp->quit(); });
    auto* settings = menuBar()->addMenu(QStringLiteral("设置与备份"));
    settings->addAction(QStringLiteral("偏好设置"), this, [this] { preferences(); });
    settings->addAction(QStringLiteral("导出备份…"), this, [this] {
        for (const auto& frame : frames_) if (frame) flushPages(frame);
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出配置备份"), "CrossDesk-backup.json", "JSON (*.json)");
        if (path.isEmpty()) return; QString error; notify(store_.exportBackup(path, &error) ? QStringLiteral("备份已导出") : error);
    });
    settings->addAction(QStringLiteral("恢复备份…"), this, [this] { importBackup(); });
    settings->addAction(QStringLiteral("打开数据目录"), this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(store_.directory())); });
    tray_.setIcon(style()->standardIcon(QStyle::SP_ComputerIcon)); tray_.setToolTip(QStringLiteral("CrossDesk"));
    auto* trayMenu = new QMenu(this); trayMenu->addAction(QStringLiteral("打开总览"), this, [this] { showNormal(); raise(); activateWindow(); });
    trayMenu->addAction(QStringLiteral("显示全部格子"), this, [this] { for (const auto& frame : frames_) if (frame) frame->show(); });
    trayMenu->addAction(QStringLiteral("退出"), this, [this] { quitting_ = true; qApp->quit(); }); tray_.setContextMenu(trayMenu);
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) toggleOverview();
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) tray_.show();
    connect(&store_, &Store::changed, this, [this] { QTimer::singleShot(0, this, [this] { refreshList(); }); });
    connect(&store_, &Store::persistenceFailed, this, [this](const QString& error) { notify(error); });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { quitting_ = true; });
    connect(qApp, &QGuiApplication::screenAdded, this, [this] { restoreScreenLayout(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] { restoreScreenLayout(); });
    reminderTimer_.setInterval(30000); connect(&reminderTimer_, &QTimer::timeout, this, [this] { reminders(); }); reminderTimer_.start();
    QTimer::singleShot(1000, this, [this] { reminders(); });
    applyTheme(); refreshList(); rebuildFrames();
}
Workbench::~Workbench() {
    quitting_ = true;
    for (const auto& frame : frames_) if (frame) { frame->setShuttingDown(); delete frame; }
}
void Workbench::notify(const QString& message) { statusBar()->showMessage(message, 12000); }
void Workbench::setShortcutStatus(const QString& message) { shortcut_->setText(message); }
void Workbench::toggleOverview() {
    if (isVisible() && !isMinimized()) hide(); else { showNormal(); raise(); activateWindow(); }
}
void Workbench::shortcutAction(const QString& action) {
    if (auto* dialog = QApplication::activeModalWidget()) { dialog->raise(); dialog->activateWindow(); return; }
    if (action == "overview") { toggleOverview(); return; }
    if (action == "widgets") {
        bool visible = false; for (const auto& frame : frames_) if (frame && frame->isVisible()) visible = true;
        for (const auto& frame : frames_) if (frame) frame->setVisible(!visible);
        return;
    }
    const auto kind = action == "note" ? QString("note") : QString("search");
    for (const auto& frame : frames_) if (frame && frame->focusKind(kind, kind == "note")) return;
    create(kind);
    for (const auto& frame : frames_) if (frame && frame->focusKind(kind, kind == "note")) return;
}
void Workbench::closeEvent(QCloseEvent* event) {
    if (!quitting_ && tray_.isVisible() && store_.settings()["closeToTray"].toBool(true)) { hide(); event->ignore(); }
    else { quitting_ = true; event->accept(); qApp->quit(); }
}
QStringList Workbench::selectedIds() const {
    QStringList ids; for (auto* row : list_->selectedItems()) ids << row->data(Qt::UserRole).toString(); return ids;
}
void Workbench::refreshList() {
    const auto selected = selectedIds(); QSignalBlocker blocker(list_); list_->clear();
    for (const auto& value : store_.widgets()) {
        const auto config = value.toObject();
        auto* row = new QListWidgetItem(config["title"].toString() + "   ·   " + kindTitle(config["kind"].toString()) +
            (config["group"].toString().isEmpty() ? QString{} : QStringLiteral("   ·   标签组")), list_);
        row->setData(Qt::UserRole, config["id"].toString()); row->setSelected(selected.contains(config["id"].toString()));
        row->setToolTip(config["data"].toObject()["path"].toString());
    }
    summary_->setText(QStringLiteral("%1 个格子 · %2 条待办 · %3 篇随记").arg(store_.widgets().size()).arg(store_.records("todos").size()).arg(store_.records("notes").size()));
}
void Workbench::create(const QString& kind) {
    QJsonObject data;
    if (kind == "folder") {
        const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择要映射的文件夹")); if (path.isEmpty()) return; data["path"] = path;
    }
    if (!store_.addWidget(kind, kindTitle(kind), data).isEmpty()) rebuildFrames();
}
void Workbench::rebuildFrames() {
    for (const auto& frame : frames_) if (frame) { frame->setShuttingDown(); delete frame; } frames_.clear();
    QMap<QString, QStringList> groups;
    for (const auto& value : store_.widgets()) {
        const auto config = value.toObject(); if (config["hidden"].toBool()) continue;
        const auto id = config["id"].toString(); const auto group = config["group"].toString();
        groups[group.isEmpty() ? id : group] << id;
    }
    for (const auto& ids : groups) {
        auto* frame = new WidgetFrame(store_, ids, [this](const QString& message) { notify(message); });
        frames_ << frame; frame->show();
    }
}
void Workbench::showSelected() {
    const auto ids = selectedIds();
    for (const auto& id : ids) { auto config = store_.widget(id); config["hidden"] = false; store_.putWidget(config); }
    rebuildFrames();
}
void Workbench::groupSelected(bool group) {
    const auto ids = selectedIds(); if (ids.isEmpty() || (group && ids.size() < 2)) { notify(QStringLiteral("合组时请至少选择两个格子")); return; }
    const auto groupId = group ? QUuid::createUuid().toString() : QString{};
    for (const auto& id : ids) { auto config = store_.widget(id); config["group"] = groupId; config["hidden"] = false; store_.putWidget(config); }
    rebuildFrames();
}
void Workbench::preferences() {
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("偏好设置")); auto* layout = new QFormLayout(&dialog);
    auto settings = store_.settings(); auto* theme = new QComboBox;
    theme->addItems({QStringLiteral("浅色"), QStringLiteral("深色")}); theme->setCurrentIndex(settings["dark"].toBool() ? 1 : 0); layout->addRow(QStringLiteral("外观"), theme);
    auto* opacity = new QSlider(Qt::Horizontal); opacity->setRange(35, 100); opacity->setValue(qRound(settings["opacity"].toDouble(1.0) * 100)); layout->addRow(QStringLiteral("格子不透明度"), opacity);
    auto* hover = new QCheckBox(QStringLiteral("悬停展开收起的格子")); hover->setChecked(settings["hoverReveal"].toBool()); layout->addRow(hover);
    auto* tray = new QCheckBox(QStringLiteral("关闭总览时保留托盘")); tray->setChecked(settings["closeToTray"].toBool(true)); layout->addRow(tray);
    auto* autoStart = new QCheckBox(QStringLiteral("登录 Windows 时启动")); autoStart->setChecked(settings["autoStart"].toBool()); layout->addRow(autoStart);
    QMap<QString, QKeySequenceEdit*> shortcuts;
    for (const auto& item : {qMakePair(QString("overview"), QString("Ctrl+Alt+M")), qMakePair(QString("widgets"), QString("F7")),
                            qMakePair(QString("note"), QString("Ctrl+Alt+N")), qMakePair(QString("search"), QString("Ctrl+Alt+F"))}) {
        auto* edit = new QKeySequenceEdit(QKeySequence(settings["shortcut_" + item.first].toString(item.second)));
        const QMap<QString, QString> labels{{"overview", QStringLiteral("总览快捷键")}, {"widgets", QStringLiteral("格子快捷键")},
            {"note", QStringLiteral("随记快捷键")}, {"search", QStringLiteral("搜索快捷键")}};
        layout->addRow(labels.value(item.first), edit); shortcuts[item.first] = edit;
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    if (autoStart->isChecked() != settings["autoStart"].toBool()) {
        QString error; if (!setAutoStart(autoStart->isChecked(), QCoreApplication::applicationFilePath(), &error)) { notify(error); return; }
    }
    settings["dark"] = theme->currentIndex() == 1; settings["opacity"] = opacity->value() / 100.0;
    settings["hoverReveal"] = hover->isChecked(); settings["closeToTray"] = tray->isChecked(); settings["autoStart"] = autoStart->isChecked();
    for (auto it = shortcuts.cbegin(); it != shortcuts.cend(); ++it) settings["shortcut_" + it.key()] = it.value()->keySequence().toString(QKeySequence::PortableText);
    if (store_.setSettings(settings)) { applyTheme(); for (const auto& frame : frames_) if (frame) frame->updateOpacity(opacity->value() / 100.0); }
}
void Workbench::applyTheme() {
    const bool dark = store_.settings()["dark"].toBool();
    const QString background = dark ? "#202629" : "#f3f5f3";
    const QString surface = dark ? "#2d3539" : "#ffffff";
    const QString foreground = dark ? "#e6ece9" : "#273936";
    qApp->setStyleSheet(QString("QWidget { color:%1; background:%2; font-family:'Microsoft YaHei UI'; font-size:13px; }"
        "QPushButton,QToolButton,QComboBox { background:%3; border:1px solid %4; border-radius:6px; padding:7px; }"
        "QPushButton:hover,QToolButton:hover { border-color:#478b79; }"
        "QLineEdit,QPlainTextEdit,QTextBrowser,QListView,QListWidget { background:%3; border:1px solid %4; border-radius:6px; padding:6px; }"
        "QListWidget::item { padding:9px; } QListWidget::item:selected { background:#347c68; color:white; }"
        "QMenuBar,QMenu,QStatusBar { background:%2; }")
        .arg(foreground, background, surface, dark ? "#45504e" : "#d6dfda"));
}
void Workbench::organize() {
    const auto folder = QFileDialog::getExistingDirectory(this, QStringLiteral("选择要整理的文件夹")); if (folder.isEmpty()) return;
    organizeFolder(store_, folder, this, [this](const QString& message) { notify(message); });
}
void Workbench::undoOrganization() {
    auto operations = store_.records("operations");
    for (int i = operations.size() - 1; i >= 0; --i) {
        auto operation = operations[i].toObject(); if (operation["undone"].toBool()) continue;
        const auto result = movePath(operation["destination"].toString(), operation["source"].toString());
        if (result.ok) { operation["undone"] = true; operations[i] = operation; store_.setRecords("operations", operations); notify(QStringLiteral("已撤销一次文件移动")); }
        else notify(result.error); return;
    }
    notify(QStringLiteral("没有可撤销的文件移动"));
}
void Workbench::importBackup() {
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("恢复配置备份"), {}, "JSON (*.json)"); if (path.isEmpty()) return;
    if (QMessageBox::question(this, QStringLiteral("恢复配置"), QStringLiteral("将用备份替换当前格子和记录，旧配置保存在自动备份中。继续？")) != QMessageBox::Yes) return;
    // Flush editors and release frames before replacing their backing records.
    for (const auto& frame : frames_) if (frame) { frame->setShuttingDown(); delete frame; } frames_.clear();
    QString error; const bool ok = store_.importBackup(path, &error); applyTheme(); rebuildFrames(); notify(ok ? QStringLiteral("配置已恢复") : error);
}
void Workbench::reminders() {
    const auto candidates = dueReminders(store_.records("todos"), QDateTime::currentDateTime(), notified_);
    if (candidates.isEmpty()) return;
    QStringList titles;
    for (const auto& reminder : candidates) { notified_.insert(reminder.key); titles << reminder.title; }
    const auto message = titles.mid(0, 5).join('\n') + (titles.size() > 5 ? QStringLiteral("\n共 %1 条待办到期").arg(titles.size()) : QString{});
    if (tray_.isVisible()) tray_.showMessage(QStringLiteral("待办提醒"), message, QSystemTrayIcon::Information, 10000);
    else {
        auto* dialog = new QMessageBox(QMessageBox::Information, QStringLiteral("待办提醒"), message, QMessageBox::Ok, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowModality(Qt::NonModal); dialog->show();
    }
    notify(QStringLiteral("待办到期：") + titles.join(QStringLiteral("、")));
}
void Workbench::restoreScreenLayout() {
    QTimer::singleShot(300, this, [this] {
        if (QApplication::activeModalWidget()) restoreScreenLayout(); else rebuildFrames();
    });
}
} // namespace crossdesk::desktop
