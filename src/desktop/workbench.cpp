#include <QCoreApplication>
#include "desktop/workbench.h"
#include "desktop/pages.h"
#include "desktop/shell.h"
#include "desktop/appearance.h"

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
#include <QPainter>
#include <QPixmapCache>
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
        setWindowTitle(ids_.size() > 1 ? QCoreApplication::translate("CrossDesk", "格子组") : config["title"].toString());
        setMinimumSize(240, 150);
        auto* layout = new QVBoxLayout(this); layout->setContentsMargins(8, 8, 8, 8);
        auto* tools = new QHBoxLayout;
        auto* title = new QLabel(windowTitle()); title->setTextInteractionFlags(Qt::TextSelectableByMouse); tools->addWidget(title, 1);
        collapse_ = new QToolButton; collapse_->setText(QCoreApplication::translate("CrossDesk", "收起")); tools->addWidget(collapse_);
        auto* pin = new QToolButton; pin->setText(QCoreApplication::translate("CrossDesk", "置顶")); pin->setCheckable(true); pin->setChecked(config["pinned"].toBool()); tools->addWidget(pin);
        auto* rename = new QToolButton; rename->setText(QCoreApplication::translate("CrossDesk", "名称")); tools->addWidget(rename); layout->addLayout(tools);
        auto* appearance = new QToolButton; appearance->setText(QCoreApplication::translate("CrossDesk", "外观")); tools->addWidget(appearance);
        connect(appearance, &QToolButton::clicked, this, [this] { if (editAppearance(store_, this, ids_.first())) updateAppearance(); });
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
            bool ok = false; const auto text = QInputDialog::getText(this, QCoreApplication::translate("CrossDesk", "格子名称"), QCoreApplication::translate("CrossDesk", "名称"), QLineEdit::Normal, windowTitle(), &ok);
            if (!ok || text.trimmed().isEmpty()) return;
            setWindowTitle(text.trimmed()); title->setText(windowTitle());
            auto config = store_.widget(ids_.first()); config["title"] = windowTitle(); store_.putWidget(config);
        });
        saveTimer_.setSingleShot(true); saveTimer_.setInterval(250);
        connect(&saveTimer_, &QTimer::timeout, this, [this] { persist(); });
        ready_ = true;
        updateAppearance();
        cacheTimer_.setInterval(10000);
        connect(&cacheTimer_, &QTimer::timeout, this, [this] {
            const int seconds = store_.settings()["cacheIdleSeconds"].toInt(120);
            if (!isVisible() && seconds > 0 && hiddenAt_.isValid() && hiddenAt_.secsTo(QDateTime::currentDateTime()) >= seconds) image_ = {};
        }); cacheTimer_.start();
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
    void updateAppearance() {
        appearance_ = effectiveAppearance(store_, ids_.first()); image_ = appearanceImage(appearance_);
        QString style = appearanceStyle(appearance_, "#desktopWidget, #desktopWidget QWidget");
        if (!image_.isNull()) style += "#desktopWidget QWidget {background:transparent;} #desktopWidget QLineEdit,#desktopWidget QListView,#desktopWidget QPlainTextEdit,#desktopWidget QTextBrowser,#desktopWidget QTreeWidget {background:rgba(255,255,255,190);color:#273936;}";
        setStyleSheet(style);
        const auto icon = appearance_["icon"].toString();
        if (QFileInfo::exists(icon)) setWindowIcon(QIcon(icon));
        else if (!icon.isEmpty()) { QPixmap pixmap(48, 48); pixmap.fill(Qt::transparent); QPainter p(&pixmap); QFont font = p.font(); font.setPixelSize(32); p.setFont(font); p.drawText(pixmap.rect(), Qt::AlignCenter, icon.left(8)); p.end(); setWindowIcon(QIcon(pixmap)); }
        update();
    }
protected:
    void paintEvent(QPaintEvent* event) override { QWidget::paintEvent(event); QPainter painter(this); paintAppearance(painter, rect(), appearance_, image_); }
    void hideEvent(QHideEvent* event) override { hiddenAt_ = QDateTime::currentDateTime(); QWidget::hideEvent(event); }
    void showEvent(QShowEvent* event) override { QWidget::showEvent(event); if (image_.isNull() && !appearance_["background"].toString().isEmpty()) { image_ = appearanceImage(appearance_); update(); } }
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
        body_->setVisible(!small); collapse_->setText(small ? QCoreApplication::translate("CrossDesk", "展开") : QCoreApplication::translate("CrossDesk", "收起"));
        setMinimumHeight(small ? 48 : 150);
        const QRect target(x(), y(), width(), small ? 52 : qMax(150, expandedHeight_));
        animation_.stop();
        if (animate && !store_.settings()["reducedMotion"].toBool()) {
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
    QJsonObject appearance_; QImage image_;
    QTimer cacheTimer_; QDateTime hiddenAt_;
    QTimer saveTimer_; QPropertyAnimation animation_; bool collapsed_ = false;
    bool hoverExpanded_ = false; bool ready_ = false; bool shuttingDown_ = false; int expandedHeight_ = 320;
};

Workbench::Workbench(Store& store, QWidget* parent) : QMainWindow(parent), store_(store) {
    setWindowTitle(QStringLiteral("CrossDesk")); resize(920, 660); setMinimumSize(680, 480);
    auto* body = new QWidget; auto* layout = new QVBoxLayout(body); layout->setContentsMargins(28, 24, 28, 20); layout->setSpacing(16);
    auto* heading = new QLabel(QCoreApplication::translate("CrossDesk", "让桌面各归其位")); heading->setStyleSheet("font-size:28px;font-weight:600;"); layout->addWidget(heading);
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
    addAction(QCoreApplication::translate("CrossDesk", "显示所选"), [this] { showSelected(); });
    addAction(QCoreApplication::translate("CrossDesk", "合成标签组"), [this] { groupSelected(true); });
    addAction(QCoreApplication::translate("CrossDesk", "拆分标签组"), [this] { groupSelected(false); });
    addAction(QCoreApplication::translate("CrossDesk", "删除格子"), [this] {
        const auto ids = selectedIds(); if (ids.isEmpty()) return;
        if (QMessageBox::question(this, QCoreApplication::translate("CrossDesk", "删除格子"), QCoreApplication::translate("CrossDesk", "只移除格子，文件和待办随记仍保留。继续？")) != QMessageBox::Yes) return;
        for (const auto& id : ids) store_.removeWidget(id); rebuildFrames();
    });
    tools->addStretch(); addAction(QCoreApplication::translate("CrossDesk", "窗口总览"), [this] { if (openWindowOverview) openWindowOverview(); else notify(QCoreApplication::translate("CrossDesk", "当前构建未启用窗口总览")); });
    layout->addLayout(tools); shortcut_ = new QLabel; shortcut_->setWordWrap(true); layout->addWidget(shortcut_); setCentralWidget(body);
    auto* file = menuBar()->addMenu(QCoreApplication::translate("CrossDesk", "桌面"));
    file->addAction(QCoreApplication::translate("CrossDesk", "显示全部格子"), this, [this] {
        for (const auto& value : store_.widgets()) { auto config = value.toObject(); config["hidden"] = false; store_.putWidget(config); }
        rebuildFrames();
    });
    file->addAction(QCoreApplication::translate("CrossDesk", "收起 / 显示所有格子"), this, [this] {
        bool anyVisible = false; for (const auto& frame : frames_) if (frame && frame->isVisible()) anyVisible = true;
        for (const auto& frame : frames_) if (frame) frame->setVisible(!anyVisible);
    });
    file->addAction(QCoreApplication::translate("CrossDesk", "整理文件夹…"), this, [this] { organize(); });
    file->addAction(QCoreApplication::translate("CrossDesk", "撤销上次文件移动"), this, [this] { undoOrganization(); });
    file->addSeparator(); file->addAction(QCoreApplication::translate("CrossDesk", "退出"), this, [this] { quitting_ = true; qApp->quit(); });
    auto* settings = menuBar()->addMenu(QCoreApplication::translate("CrossDesk", "设置与备份"));
    settings->addAction(QCoreApplication::translate("CrossDesk", "偏好设置"), this, [this] { preferences(); });
    settings->addAction(QCoreApplication::translate("CrossDesk", "字体、配色与背景"), this, [this] { if (editAppearance(store_, this)) { applyTheme(); for (const auto& frame : frames_) if (frame) frame->updateAppearance(); } });
    settings->addAction(QCoreApplication::translate("CrossDesk", "性能与诊断"), this, [this] { showMaintenance(store_, this); });
    settings->addAction(QCoreApplication::translate("CrossDesk", "导出备份…"), this, [this] {
        for (const auto& frame : frames_) if (frame) flushPages(frame);
        const auto path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("CrossDesk", "导出配置备份"), "CrossDesk-backup.json", "JSON (*.json)");
        if (path.isEmpty()) return; QString error; notify(store_.exportBackup(path, &error) ? QCoreApplication::translate("CrossDesk", "备份已导出") : error);
    });
    settings->addAction(QCoreApplication::translate("CrossDesk", "恢复备份…"), this, [this] { importBackup(); });
    settings->addAction(QCoreApplication::translate("CrossDesk", "打开数据目录"), this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(store_.directory())); });
    tray_.setIcon(style()->standardIcon(QStyle::SP_ComputerIcon)); tray_.setToolTip(QStringLiteral("CrossDesk"));
    auto* trayMenu = new QMenu(this); trayMenu->addAction(QCoreApplication::translate("CrossDesk", "打开总览"), this, [this] { showNormal(); raise(); activateWindow(); });
    trayMenu->addAction(QCoreApplication::translate("CrossDesk", "显示全部格子"), this, [this] { for (const auto& frame : frames_) if (frame) frame->show(); });
    trayMenu->addAction(QCoreApplication::translate("CrossDesk", "退出"), this, [this] { quitting_ = true; qApp->quit(); }); tray_.setContextMenu(trayMenu);
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
            (config["group"].toString().isEmpty() ? QString{} : QCoreApplication::translate("CrossDesk", "   ·   标签组")), list_);
        row->setData(Qt::UserRole, config["id"].toString()); row->setSelected(selected.contains(config["id"].toString()));
        row->setToolTip(config["data"].toObject()["path"].toString());
    }
    summary_->setText(QCoreApplication::translate("CrossDesk", "%1 个格子 · %2 条待办 · %3 篇随记").arg(store_.widgets().size()).arg(store_.records("todos").size()).arg(store_.records("notes").size()));
}
void Workbench::create(const QString& kind) {
    QJsonObject data;
    if (kind == "folder") {
        const auto path = QFileDialog::getExistingDirectory(this, QCoreApplication::translate("CrossDesk", "选择要映射的文件夹")); if (path.isEmpty()) return; data["path"] = path;
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
    const auto ids = selectedIds(); if (ids.isEmpty() || (group && ids.size() < 2)) { notify(QCoreApplication::translate("CrossDesk", "合组时请至少选择两个格子")); return; }
    const auto groupId = group ? QUuid::createUuid().toString() : QString{};
    for (const auto& id : ids) { auto config = store_.widget(id); config["group"] = groupId; config["hidden"] = false; store_.putWidget(config); }
    rebuildFrames();
}
void Workbench::preferences() {
    QDialog dialog(this); dialog.setWindowTitle(QCoreApplication::translate("CrossDesk", "偏好设置")); auto* layout = new QFormLayout(&dialog);
    auto settings = store_.settings(); auto* theme = new QComboBox;
    theme->addItems({QCoreApplication::translate("CrossDesk", "浅色"), QCoreApplication::translate("CrossDesk", "深色")}); theme->setCurrentIndex(settings["dark"].toBool() ? 1 : 0); layout->addRow(QCoreApplication::translate("CrossDesk", "外观"), theme);
    auto* language = new QComboBox; language->addItem(QCoreApplication::translate("CrossDesk", "简体中文"), "zh_CN"); language->addItem("English", "en"); language->setCurrentIndex(qMax(0, language->findData(settings["language"].toString("zh_CN")))); layout->addRow(QCoreApplication::translate("CrossDesk", "语言（重启生效）"), language);
    auto* opacity = new QSlider(Qt::Horizontal); opacity->setRange(35, 100); opacity->setValue(qRound(settings["opacity"].toDouble(1.0) * 100)); layout->addRow(QCoreApplication::translate("CrossDesk", "格子不透明度"), opacity);
    auto* hover = new QCheckBox(QCoreApplication::translate("CrossDesk", "悬停展开收起的格子")); hover->setChecked(settings["hoverReveal"].toBool()); layout->addRow(hover);
    auto* tray = new QCheckBox(QCoreApplication::translate("CrossDesk", "关闭总览时保留托盘")); tray->setChecked(settings["closeToTray"].toBool(true)); layout->addRow(tray);
    auto* autoStart = new QCheckBox(QCoreApplication::translate("CrossDesk", "登录 Windows 时启动")); autoStart->setChecked(settings["autoStart"].toBool()); layout->addRow(autoStart);
    QMap<QString, QKeySequenceEdit*> shortcuts;
    for (const auto& item : {qMakePair(QString("overview"), QString("Ctrl+Alt+M")), qMakePair(QString("widgets"), QString("F7")),
                            qMakePair(QString("note"), QString("Ctrl+Alt+N")), qMakePair(QString("search"), QString("Ctrl+Alt+F"))}) {
        auto* edit = new QKeySequenceEdit(QKeySequence(settings["shortcut_" + item.first].toString(item.second)));
        const QMap<QString, QString> labels{{"overview", QCoreApplication::translate("CrossDesk", "总览快捷键")}, {"widgets", QCoreApplication::translate("CrossDesk", "格子快捷键")},
            {"note", QCoreApplication::translate("CrossDesk", "随记快捷键")}, {"search", QCoreApplication::translate("CrossDesk", "搜索快捷键")}};
        layout->addRow(labels.value(item.first), edit); shortcuts[item.first] = edit;
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    if (autoStart->isChecked() != settings["autoStart"].toBool()) {
        QString error; if (!setAutoStart(autoStart->isChecked(), QCoreApplication::applicationFilePath(), &error)) { notify(error); return; }
    }
    settings["dark"] = theme->currentIndex() == 1; settings["opacity"] = opacity->value() / 100.0;
    settings["language"] = language->currentData().toString();
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
        "QCheckBox::indicator,QListView::indicator,QTreeView::indicator {width:16px;height:16px;border:1px solid %4;border-radius:3px;background:%3;}"
        "QCheckBox::indicator:checked,QListView::indicator:checked,QTreeView::indicator:checked {background:#347c68;border:2px solid #21604f;}"
        "QListWidget::indicator {margin-right:6px;}"
        "QMenuBar,QMenu,QStatusBar { background:%2; }")
        .arg(foreground, background, surface, dark ? "#45504e" : "#d6dfda") + appearanceStyle(effectiveAppearance(store_), "QWidget"));
    QPixmapCache::setCacheLimit(std::clamp(store_.settings()["cacheLimitMB"].toInt(32), 8, 256) * 1024);
}
void Workbench::organize() {
    const auto folder = QFileDialog::getExistingDirectory(this, QCoreApplication::translate("CrossDesk", "选择要整理的文件夹")); if (folder.isEmpty()) return;
    organizeFolder(store_, folder, this, [this](const QString& message) { notify(message); });
}
void Workbench::undoOrganization() {
    auto operations = store_.records("operations");
    for (int i = operations.size() - 1; i >= 0; --i) {
        auto operation = operations[i].toObject(); if (operation["undone"].toBool()) continue;
        const auto result = movePath(operation["destination"].toString(), operation["source"].toString());
        if (result.ok) { operation["undone"] = true; operations[i] = operation; store_.setRecords("operations", operations); notify(QCoreApplication::translate("CrossDesk", "已撤销一次文件移动")); }
        else notify(result.error); return;
    }
    notify(QCoreApplication::translate("CrossDesk", "没有可撤销的文件移动"));
}
void Workbench::importBackup() {
    const auto path = QFileDialog::getOpenFileName(this, QCoreApplication::translate("CrossDesk", "恢复配置备份"), {}, "JSON (*.json)"); if (path.isEmpty()) return;
    if (QMessageBox::question(this, QCoreApplication::translate("CrossDesk", "恢复配置"), QCoreApplication::translate("CrossDesk", "将用备份替换当前格子和记录，旧配置保存在自动备份中。继续？")) != QMessageBox::Yes) return;
    // Flush editors and release frames before replacing their backing records.
    for (const auto& frame : frames_) if (frame) { frame->setShuttingDown(); delete frame; } frames_.clear();
    QString error; const bool ok = store_.importBackup(path, &error); applyTheme(); rebuildFrames(); notify(ok ? QCoreApplication::translate("CrossDesk", "配置已恢复") : error);
}
void Workbench::reminders() {
    const auto candidates = dueReminders(store_.records("todos"), QDateTime::currentDateTime(), notified_);
    if (candidates.isEmpty()) return;
    QStringList titles;
    for (const auto& reminder : candidates) { notified_.insert(reminder.key); titles << reminder.title; }
    const auto message = titles.mid(0, 5).join('\n') + (titles.size() > 5 ? QCoreApplication::translate("CrossDesk", "\n共 %1 条待办到期").arg(titles.size()) : QString{});
    if (tray_.isVisible()) tray_.showMessage(QCoreApplication::translate("CrossDesk", "待办提醒"), message, QSystemTrayIcon::Information, 10000);
    else {
        auto* dialog = new QMessageBox(QMessageBox::Information, QCoreApplication::translate("CrossDesk", "待办提醒"), message, QMessageBox::Ok, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowModality(Qt::NonModal); dialog->show();
    }
    notify(QCoreApplication::translate("CrossDesk", "待办到期：") + titles.join(QStringLiteral("、")));
}
void Workbench::restoreScreenLayout() {
    QTimer::singleShot(300, this, [this] {
        if (QApplication::activeModalWidget()) restoreScreenLayout(); else rebuildFrames();
    });
}
} // namespace crossdesk::desktop
