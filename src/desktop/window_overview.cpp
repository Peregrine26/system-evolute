#include "desktop/window_overview.h"
#include "platform/windows/win32_api_adapter.h"
#include "platform/windows/qt_desktop_bridge.h"
#include "window_layout.h"
#include "window_catalog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <optional>

namespace crossdesk::desktop {
namespace {
QString ui(const char* text) { return QCoreApplication::translate("CrossDesk", text); }
std::uint32_t ownPid() { return std::uint32_t(QCoreApplication::applicationPid()); }
QString identity(NativeWindowHandle handle, std::uint32_t pid) { return QString::number(qulonglong(handle)) + '/' + QString::number(pid); }
QString planError(LayoutPlanError error) {
    switch (error) {
    case LayoutPlanError::ScanIncomplete: return ui("扫描未完成，未移动窗口；请刷新重试");
    case LayoutPlanError::InvalidWorkArea: return ui("工作区不足，无法预览排列");
    case LayoutPlanError::WindowMissing: return ui("窗口已关闭或不可管理，请重新选择");
    case LayoutPlanError::IdentityChanged: return ui("窗口身份已变化，请重新选择");
    case LayoutPlanError::WindowChanged: return ui("窗口位置已变化，请重新预览后确认");
    case LayoutPlanError::WorkAreaChanged: return ui("屏幕工作区已变化，请重新预览后确认");
    default: return ui("请选择两个不同窗口");
    }
}
QRect project(const WindowBounds& bounds, const WindowBounds& area, const QRect& viewport) {
    const double width = double(area.right) - area.left, height = double(area.bottom) - area.top;
    if (width <= 0 || height <= 0 || viewport.isEmpty()) return {};
    const double scale = qMin(viewport.width() / width, viewport.height() / height);
    const double x = viewport.x() + (viewport.width() - width * scale) / 2;
    const double y = viewport.y() + (viewport.height() - height * scale) / 2;
    const int left = qRound(x + (bounds.left - double(area.left)) * scale);
    const int top = qRound(y + (bounds.top - double(area.top)) * scale);
    const int right = qRound(x + (bounds.right - double(area.left)) * scale);
    const int bottom = qRound(y + (bounds.bottom - double(area.top)) * scale);
    return {left, top, qMax(1, right - left), qMax(1, bottom - top)};
}
class PreviewPane final : public QWidget {
public:
    explicit PreviewPane(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(220); }
    std::optional<TwoWindowPlan> plan;
    std::array<bool, 2> live{{false, false}};
    std::function<void()> resized;
    std::array<QRect, 2> regions() const {
        const QRect viewport = rect().adjusted(12, 12, -12, -40);
        if (plan) return {{project(plan->targets[0].bounds, plan->work_area, viewport), project(plan->targets[1].bounds, plan->work_area, viewport)}};
        const int width = qMax(1, (viewport.width() - 12) / 2);
        return {{{viewport.left(), viewport.top(), width, viewport.height()}, {viewport.left() + width + 12, viewport.top(), qMax(1, viewport.width() - width - 12), viewport.height()}}};
    }
protected:
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); if (resized) resized(); }
    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event); QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const auto boxes = regions();
        for (int i = 0; i < 2; ++i) {
            painter.setPen(QColor("#6b988b")); painter.setBrush(QColor(85, 135, 115, 18)); painter.drawRoundedRect(boxes[i], 5, 5);
            painter.setPen(palette().color(QPalette::Text));
            if (!live[i]) painter.drawText(boxes[i].adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, i ? ui("右侧窗口预览") : ui("左侧窗口预览"));
            if (plan) {
                const auto& b = plan->targets[i].bounds;
                painter.drawText(QRect(boxes[i].left(), height() - 28, boxes[i].width(), 24), Qt::AlignHCenter | Qt::AlignTop,
                    QString("%1,%2 · %3×%4").arg(b.left).arg(b.top).arg(b.right - b.left).arg(b.bottom - b.top));
            }
        }
    }
};
}

struct WindowOverview::Impl {
    enum class Mode { Overview, Preview, Submitting };
    WindowOverview& owner;
    std::unique_ptr<Win32ApiAdapter> native;
    const WindowApiAdapter& api;
    WindowsWindowScanner scanner;
    QTreeWidget* list;
    PreviewPane* preview;
    QLabel *titles, *status, *instruction, *shortcutStatus;
    QComboBox* ratio;
    QPushButton *refresh, *locate, *begin, *confirm, *back, *swap, *tools;
    std::unique_ptr<QtDesktopBridge> bridge;
    QPropertyAnimation animation;
    Mode mode = Mode::Overview;
    bool transition = false, closing = false;
    std::optional<TwoWindowPlan> plan;
    std::array<QString, 2> names;

    Impl(WindowOverview& window, const WindowApiAdapter* injected)
        : owner(window), native(injected ? nullptr : std::make_unique<Win32ApiAdapter>()), api(injected ? *injected : *native), scanner(api) {
        auto* layout = new QVBoxLayout(&owner);
        auto* actions = new QHBoxLayout;
        refresh = new QPushButton(ui("刷新 · Ctrl+R")); refresh->setObjectName("refreshWindows"); actions->addWidget(refresh);
        locate = new QPushButton(ui("定位 · Enter")); locate->setObjectName("activateWindow"); actions->addWidget(locate);
        actions->addStretch(); tools = new QPushButton(ui("桌面工具")); tools->setObjectName("openDesktopTools"); actions->addWidget(tools); layout->addLayout(actions);
        instruction = new QLabel(ui("选择一个窗口定位，选择两个窗口预览排列；左侧为列表中靠前的窗口。")); instruction->setWordWrap(true); layout->addWidget(instruction);
        auto* controls = new QHBoxLayout;
        ratio = new QComboBox; ratio->setObjectName("layoutRatio"); ratio->addItems({ui("等宽 50:50"), ui("左侧主窗口 60:40")}); controls->addWidget(ratio);
        begin = new QPushButton(ui("预览排列 · Ctrl+Enter")); begin->setObjectName("previewLayout"); controls->addWidget(begin);
        back = new QPushButton(ui("返回总览 · Esc")); back->setObjectName("backToOverview"); controls->addWidget(back);
        swap = new QPushButton(ui("交换左右")); swap->setObjectName("swapLayoutWindows"); controls->addWidget(swap);
        confirm = new QPushButton(ui("确认排列 · Ctrl+Enter")); confirm->setObjectName("confirmLayout"); controls->addWidget(confirm); layout->addLayout(controls);
        titles = new QLabel; titles->setTextFormat(Qt::PlainText); titles->setWordWrap(true); layout->addWidget(titles);
        preview = new PreviewPane; preview->setObjectName("layoutPreview"); layout->addWidget(preview, 1);
        list = new QTreeWidget; list->setObjectName("windowList"); list->setHeaderLabels({ui("窗口"), ui("进程"), ui("位置与大小")});
        list->setColumnWidth(0, 400); list->setColumnWidth(1, 180); list->setSelectionMode(QAbstractItemView::ExtendedSelection); layout->addWidget(list, 1);
        status = new QLabel; status->setObjectName("overviewStatus"); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); layout->addWidget(status);
        shortcutStatus = new QLabel; shortcutStatus->setObjectName("overviewShortcutStatus"); shortcutStatus->setTextFormat(Qt::PlainText); shortcutStatus->setWordWrap(true); layout->addWidget(shortcutStatus);
        bridge = std::make_unique<QtDesktopBridge>(owner);
        preview->resized = [this] { QTimer::singleShot(0, &owner, [this] { previews(); }); };
        QObject::connect(refresh, &QPushButton::clicked, &owner, [this] { if (!busy()) { leavePreview(false); rescan(); } });
        QObject::connect(locate, &QPushButton::clicked, &owner, [this] { activate(); });
        QObject::connect(begin, &QPushButton::clicked, &owner, [this] { beginPreview(); });
        QObject::connect(confirm, &QPushButton::clicked, &owner, [this] { execute(); });
        QObject::connect(back, &QPushButton::clicked, &owner, [this] { if (!busy()) leavePreview(true); });
        QObject::connect(swap, &QPushButton::clicked, &owner, [this] { if (plan && !busy()) { auto ids = identities(); std::swap(ids[0], ids[1]); buildPreview(ids); } });
        QObject::connect(tools, &QPushButton::clicked, &owner, [this] { if (!busy() && owner.openDesktopTools) owner.openDesktopTools(); });
        QObject::connect(list, &QTreeWidget::itemSelectionChanged, &owner, [this] { if (mode == Mode::Overview) previews(); updateUi(); });
        QObject::connect(ratio, &QComboBox::currentIndexChanged, &owner, [this] { if (plan && !busy()) buildPreview(identities()); else previews(); });
        QObject::connect(&animation, &QPropertyAnimation::finished, &owner, [this] {
            transition = false; owner.setWindowOpacity(1.0);
            if (closing) { closing = false; owner.hide(); }
            updateUi(); previews();
        });
        auto shortcut = [this](const QKeySequence& key, const std::function<void()>& action) {
            auto* command = new QShortcut(key, &owner); command->setAutoRepeat(false); QObject::connect(command, &QShortcut::activated, &owner, action);
        };
        shortcut(QKeySequence("Ctrl+R"), [this] { if (!busy()) { leavePreview(false); rescan(); } });
        shortcut(QKeySequence(Qt::Key_Return), [this] { activate(); });
        shortcut(QKeySequence("Ctrl+Return"), [this] { if (mode == Mode::Overview) beginPreview(); else execute(); });
        shortcut(QKeySequence(Qt::Key_Escape), [this] { if (!busy()) { if (mode == Mode::Preview) leavePreview(true); else requestHide(); } });
        updateUi();
    }
    bool busy() const { return transition || mode == Mode::Submitting; }
    static NativeWindowHandle handle(QTreeWidgetItem* row) { return row->data(0, Qt::UserRole).toULongLong(); }
    static std::uint32_t pid(QTreeWidgetItem* row) { return row->data(0, Qt::UserRole + 1).toUInt(); }
    std::array<WindowIdentity, 2> identities() const { return {{{plan->targets[0].window, plan->targets[0].process_id}, {plan->targets[1].window, plan->targets[1].process_id}}}; }
    void updateUi() {
        const bool inPreview = mode != Mode::Overview;
        owner.setProperty("layoutMode", mode == Mode::Submitting ? "submitting" : inPreview ? "preview" : "overview");
        begin->setVisible(!inPreview); back->setVisible(inPreview); swap->setVisible(inPreview); confirm->setVisible(inPreview); list->setVisible(!inPreview);
        refresh->setEnabled(!busy()); locate->setEnabled(!busy() && !inPreview && list->selectedItems().size() == 1);
        ratio->setEnabled(!busy()); begin->setEnabled(!busy() && list->selectedItems().size() == 2);
        back->setEnabled(!busy()); swap->setEnabled(!busy()); confirm->setEnabled(!busy() && bool(plan)); tools->setEnabled(!busy());
        instruction->setText(inPreview ? ui("这里只预览目标位置，确认后才移动窗口。Esc 返回总览。") : ui("选择一个窗口定位，选择两个窗口预览排列；左侧为列表中靠前的窗口。"));
    }
    void animate(bool hide = false) {
        animation.stop(); bridge->clear_previews(); preview->live.fill(false); closing = hide;
        if (QApplication::platformName() == "offscreen" || owner.property("reducedMotion").toBool()) {
            transition = false; if (hide) { closing = false; owner.hide(); } updateUi(); previews(); return;
        }
        transition = true; updateUi(); animation.setTargetObject(&owner); animation.setPropertyName("windowOpacity");
        animation.setStartValue(hide ? 1.0 : 0.72); animation.setEndValue(hide ? 0.2 : 1.0); animation.setDuration(140); animation.start();
    }
    void requestHide() { if (!busy()) animate(true); }
    void leavePreview(bool animated) {
        if (mode == Mode::Submitting) return;
        plan.reset(); preview->plan.reset(); mode = Mode::Overview; updateUi();
        status->setText(ui("已返回总览，窗口位置未改变")); preview->update();
        if (animated) animate(); else previews();
    }
    void rescan() {
        const auto result = scanner.scan_with_status();
        if (!result.complete) { status->setText(ui("扫描未完成，请重试；保留上一次窗口列表")); return; }
        QStringList selected; for (auto* row : list->selectedItems()) selected << identity(handle(row), pid(row));
        QSignalBlocker blocker(list); bridge->clear_previews(); list->clear();
        for (const auto& window : applicationWindows(result.windows, ownPid())) {
            const auto& b = window.bounds;
            auto* row = new QTreeWidgetItem(list, {QString::fromStdWString(window.title), QString::fromStdWString(window.process_name),
                QString("%1,%2 · %3×%4").arg(b.left).arg(b.top).arg(b.right - b.left).arg(b.bottom - b.top)});
            row->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(window.native_handle)); row->setData(0, Qt::UserRole + 1, window.process_id);
            row->setSelected(selected.contains(identity(window.native_handle, window.process_id)));
        }
        status->setText(ui("%1 个窗口；选择一个定位，选择两个预览并排列").arg(list->topLevelItemCount())); updateUi(); previews();
    }
    void previews() {
        preview->update(); if (!owner.isVisible() || transition) return;
        std::vector<std::pair<NativeWindowHandle, QRect>> regions; QStringList names;
        const auto boxes = preview->regions(); const auto origin = preview->mapTo(&owner, QPoint(0, 0));
        if (plan) {
            for (int i = 0; i < 2; ++i) { regions.push_back({plan->targets[i].window, boxes[i].translated(origin)}); names << this->names[i]; }
        } else {
            auto rows = list->selectedItems();
            for (int i = 0; rows.size() < 2 && i < list->topLevelItemCount(); ++i) if (!rows.contains(list->topLevelItem(i))) rows << list->topLevelItem(i);
            for (int i = 0; i < qMin(2, rows.size()); ++i) { regions.push_back({handle(rows[i]), boxes[i].translated(origin)}); names << rows[i]->text(0).left(70); }
        }
        preview->live.fill(false);
        if (regions.empty()) { bridge->clear_previews(); titles->setText(ui("没有可预览的窗口")); return; }
        const auto results = bridge->set_previews(regions);
        for (int i = 0; i < int(results.size()); ++i) { preview->live[i] = results[i].ready(); names[i] += results[i].ready() ? ui(" · 实时预览") : ui(" · 此窗口暂不可预览"); }
        preview->update();
        titles->setText(names.join("     |     "));
    }
    void activate() {
        if (busy() || mode != Mode::Overview) return;
        const auto selected = list->selectedItems();
        if (selected.size() != 1) { status->setText(ui("请选择一个窗口")); return; }
        const auto window = handle(selected.first());
        if (api.get_process_id(window) != pid(selected.first()) || pid(selected.first()) == ownPid()) { status->setText(ui("窗口身份已变化，请刷新")); return; }
        const auto result = api.activate_window(window);
        status->setText(result.reached_target() ? ui("已定位窗口") : result.status == WindowOperationStatus::RequestedUnconfirmed
            ? ui("定位请求已接受，Windows 尚未确认") : ui("窗口已关闭或 Windows 拒绝置前"));
        if (result.reached_target()) { bridge->clear_previews(); owner.hide(); }
    }
    void beginPreview() {
        if (busy() || mode != Mode::Overview) return;
        const auto selected = list->selectedItems();
        if (selected.size() != 2) { status->setText(ui("请选择两个不同窗口")); return; }
        buildPreview({{{handle(selected[0]), pid(selected[0])}, {handle(selected[1]), pid(selected[1])}}});
    }
    void buildPreview(const std::array<WindowIdentity, 2>& selected) {
        WindowBounds work;
        if (!api.get_primary_work_area(work)) { status->setText(ui("无法读取工作区")); return; }
        const auto scan = scanner.scan_with_status();
        const auto result = planTwoWindows(scan, selected, work, ratio->currentIndex() ? TwoWindowMode::MainLeft : TwoWindowMode::Equal, ownPid());
        if (!result.valid()) { leavePreview(false); status->setText(planError(result.error)); return; }
        plan = result.plan; preview->plan = plan; mode = Mode::Preview;
        for (int i = 0; i < 2; ++i) for (const auto& window : scan.windows)
            if (window.native_handle == selected[i].handle && window.process_id == selected[i].process_id) names[i] = QString::fromStdWString(window.title).left(70);
        status->setText(ui("预览已就绪；确认排列后才会移动这两个窗口")); updateUi(); animate();
    }
    void execute() {
        if (busy() || mode != Mode::Preview || !plan) return;
        WindowBounds work;
        if (!api.get_primary_work_area(work)) { status->setText(ui("无法读取工作区")); return; }
        const auto error = validateTwoWindowPlan(*plan, scanner.scan_with_status(), work, ownPid());
        if (error != LayoutPlanError::None) {
            if (error != LayoutPlanError::ScanIncomplete) { leavePreview(false); rescan(); }
            status->setText(planError(error)); return;
        }
        const auto frozen = *plan; mode = Mode::Submitting; updateUi();
        auto execution = LayoutExecutor(api).submit(frozen.targets);
        if (!execution.submitted) { mode = Mode::Overview; plan.reset(); preview->plan.reset(); rescan(); status->setText(ui("选择已失效或扫描未完成，请刷新重试")); return; }
        QTimer::singleShot(180, &owner, [this, execution]() mutable {
            LayoutExecutor(api).observe(execution); QStringList reports;
            for (int i = 0; i < 2; ++i) {
                const auto& report = execution.windows[i];
                reports << ui("窗口 %1：%2").arg(i + 1).arg(report.observation == MoveObservation::Reached ? ui("已达到目标")
                    : report.observation == MoveObservation::ScanIncomplete ? ui("扫描未完成，状态未知")
                    : report.observation == MoveObservation::Missing ? ui("窗口已失效")
                    : report.request.status == WindowOperationStatus::Rejected ? ui("Windows 未接受调整请求，请查看实际位置") : ui("未达到目标，可能不支持调整大小"));
            }
            mode = Mode::Overview; plan.reset(); preview->plan.reset(); rescan(); status->setText(reports.join(ui("；"))); updateUi(); animate();
        });
    }
};

WindowOverview::WindowOverview(QWidget* parent) : QWidget(parent, Qt::Window) {
    setWindowTitle(ui("CrossDesk · 窗口总览")); resize(1000, 700); impl_ = std::make_unique<Impl>(*this, nullptr);
}
WindowOverview::WindowOverview(const WindowApiAdapter& api, QWidget* parent) : QWidget(parent, Qt::Window) {
    setWindowTitle(ui("CrossDesk · 窗口总览")); resize(1000, 700); impl_ = std::make_unique<Impl>(*this, &api);
}
WindowOverview::~WindowOverview() = default;
void WindowOverview::setShortcutStatus(const QString& message) { impl_->shortcutStatus->setText(message); }
void WindowOverview::showOverview() {
    if (auto* modal = QApplication::activeModalWidget()) { modal->raise(); modal->activateWindow(); return; }
    if (impl_->mode == Impl::Mode::Submitting) { raise(); activateWindow(); return; }
    if (impl_->mode != Impl::Mode::Submitting) { impl_->animation.stop(); impl_->transition = false; impl_->closing = false; impl_->leavePreview(false); }
    if (!isVisible() || isMinimized()) showNormal(); else { impl_->rescan(); impl_->animate(); }
    raise(); activateWindow();
}
void WindowOverview::toggleOverview() {
    if (auto* modal = QApplication::activeModalWidget()) { modal->raise(); modal->activateWindow(); return; }
    if (isVisible() && !isMinimized()) impl_->requestHide(); else showOverview();
}
void WindowOverview::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event); if (impl_) QTimer::singleShot(0, this, [this] { impl_->previews(); });
}
void WindowOverview::showEvent(QShowEvent* event) {
    QWidget::showEvent(event); if (impl_->mode != Impl::Mode::Submitting) impl_->rescan(); impl_->animate();
}
void WindowOverview::hideEvent(QHideEvent* event) { impl_->bridge->clear_previews(); QWidget::hideEvent(event); }
void WindowOverview::closeEvent(QCloseEvent* event) { event->ignore(); if (closeRequested) closeRequested(); else impl_->requestHide(); }
} // namespace crossdesk::desktop
