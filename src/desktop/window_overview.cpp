#include "desktop/window_overview.h"
#include "platform/windows/win32_api_adapter.h"
#include "platform/windows/qt_desktop_bridge.h"
#include "platform/layout_executor.h"
#include "windows_window_scanner.h"
#include "window_catalog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace crossdesk::desktop {
struct WindowOverview::Impl {
    WindowOverview& owner;
    Win32ApiAdapter api;
    WindowsWindowScanner scanner{api};
    QTreeWidget* list;
    QWidget* preview;
    QLabel* titles;
    QLabel* status;
    QComboBox* ratio;
    std::unique_ptr<QtDesktopBridge> bridge;
    QPropertyAnimation animation;
    explicit Impl(WindowOverview& window) : owner(window) {
        auto* layout = new QVBoxLayout(&owner);
        auto* tools = new QHBoxLayout;
        auto* refresh = new QPushButton(QStringLiteral("刷新 · Ctrl+R")); tools->addWidget(refresh);
        auto* locate = new QPushButton(QStringLiteral("定位 · Enter")); tools->addWidget(locate);
        ratio = new QComboBox; ratio->addItems({QStringLiteral("等宽 50:50"), QStringLiteral("左侧主窗口 60:40")}); tools->addWidget(ratio);
        auto* arrange = new QPushButton(QStringLiteral("确认排列 · Ctrl+Enter")); tools->addWidget(arrange); layout->addLayout(tools);
        titles = new QLabel; titles->setWordWrap(true); layout->addWidget(titles);
        preview = new QWidget; preview->setMinimumHeight(240); layout->addWidget(preview, 1);
        list = new QTreeWidget; list->setHeaderLabels({QStringLiteral("窗口"), QStringLiteral("进程"), QStringLiteral("位置与大小")});
        list->setColumnWidth(0, 400); list->setColumnWidth(1, 180); list->setSelectionMode(QAbstractItemView::ExtendedSelection); layout->addWidget(list, 1);
        status = new QLabel; status->setWordWrap(true); layout->addWidget(status);
        bridge = std::make_unique<QtDesktopBridge>(owner);
        QObject::connect(refresh, &QPushButton::clicked, &owner, [this] { rescan(); });
        QObject::connect(locate, &QPushButton::clicked, &owner, [this] { activate(); });
        QObject::connect(arrange, &QPushButton::clicked, &owner, [this] { move(); });
        QObject::connect(list, &QTreeWidget::itemSelectionChanged, &owner, [this] { previews(); });
        QObject::connect(ratio, &QComboBox::currentIndexChanged, &owner, [this] { previews(); });
        auto shortcut = [this](const QKeySequence& key, const std::function<void()>& action) {
            auto* command = new QShortcut(key, &owner); QObject::connect(command, &QShortcut::activated, &owner, action);
        };
        shortcut(QKeySequence::Refresh, [this] { rescan(); });
        shortcut(QKeySequence("Ctrl+R"), [this] { rescan(); });
        shortcut(QKeySequence(Qt::Key_Return), [this] { activate(); });
        shortcut(QKeySequence("Ctrl+Return"), [this] { move(); });
        shortcut(QKeySequence(Qt::Key_Escape), [this] { owner.hide(); });
    }
    static NativeWindowHandle handle(QTreeWidgetItem* row) { return row->data(0, Qt::UserRole).toULongLong(); }
    void rescan() {
        const auto result = scanner.scan_with_status();
        if (!result.complete) { status->setText(QStringLiteral("扫描未完成，请重试；保留上一次窗口列表")); return; }
        QList<qulonglong> selected; for (auto* row : list->selectedItems()) selected << handle(row);
        QSignalBlocker blocker(list); bridge->clear_previews(); list->clear();
        for (const auto& window : applicationWindows(result.windows, std::uint32_t(QCoreApplication::applicationPid()))) {
            const auto& b = window.bounds;
            auto* row = new QTreeWidgetItem(list, {QString::fromStdWString(window.title), QString::fromStdWString(window.process_name),
                QString("%1,%2 · %3×%4").arg(b.left).arg(b.top).arg(b.right - b.left).arg(b.bottom - b.top)});
            row->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(window.native_handle));
            row->setData(0, Qt::UserRole + 1, window.process_id); row->setSelected(selected.contains(window.native_handle));
        }
        status->setText(QStringLiteral("%1 个窗口；选择一个定位，选择两个预览并排列").arg(list->topLevelItemCount()));
        previews();
    }
    void previews() {
        if (!owner.isVisible()) return;
        auto selected = list->selectedItems();
        for (int i = 0; selected.size() < 2 && i < list->topLevelItemCount(); ++i)
            if (!selected.contains(list->topLevelItem(i))) selected << list->topLevelItem(i);
        if (selected.isEmpty()) { bridge->clear_previews(); titles->setText(QStringLiteral("没有可预览的窗口")); return; }
        const auto origin = preview->mapTo(&owner, QPoint(0, 0));
        const int leftWidth = (preview->width() - 12) * (ratio->currentIndex() ? 60 : 50) / 100;
        std::vector<std::pair<NativeWindowHandle, QRect>> regions;
        QStringList names;
        for (int i = 0; i < qMin(2, selected.size()); ++i) {
            const int x = i ? leftWidth + 12 : 0;
            regions.push_back({handle(selected[i]), QRect(origin + QPoint(x, 0), QSize(i ? preview->width() - x : leftWidth, preview->height()))});
        }
        const auto result = bridge->set_previews(regions);
        for (int i = 0; i < int(result.size()); ++i)
            names << selected[i]->text(0).left(60) + (result[i].ready() ? QStringLiteral(" · 实时预览") : QStringLiteral(" · 此窗口暂不可预览"));
        titles->setText(names.join(QStringLiteral("     |     ")));
    }
    void activate() {
        const auto selected = list->selectedItems();
        if (selected.size() != 1) { status->setText(QStringLiteral("请选择一个窗口")); return; }
        const auto window = handle(selected.first());
        if (api.get_process_id(window) != selected.first()->data(0, Qt::UserRole + 1).toUInt()) {
            status->setText(QStringLiteral("窗口身份已变化，请刷新")); return;
        }
        const auto result = api.activate_window(window);
        status->setText(result.reached_target() ? QStringLiteral("已定位窗口") : result.status == WindowOperationStatus::RequestedUnconfirmed
            ? QStringLiteral("定位请求已接受，Windows 尚未确认") : QStringLiteral("窗口已关闭或 Windows 拒绝置前"));
    }
    void move() {
        const auto selected = list->selectedItems();
        if (selected.size() != 2) { status->setText(QStringLiteral("请选择两个不同窗口")); return; }
        WindowBounds work;
        if (!api.get_primary_work_area(work)) { status->setText(QStringLiteral("无法读取工作区")); return; }
        const auto usable = static_cast<long long>(work.right) - work.left - 12;
        if (usable < 2 || work.bottom <= work.top) { status->setText(QStringLiteral("工作区不足")); return; }
        const int left = int(usable * (ratio->currentIndex() ? 60 : 50) / 100);
        const std::array<WindowMoveTarget, 2> targets{{
            {handle(selected[0]), selected[0]->data(0, Qt::UserRole + 1).toUInt(), {work.left, work.top, work.left + left, work.bottom}},
            {handle(selected[1]), selected[1]->data(0, Qt::UserRole + 1).toUInt(), {work.left + left + 12, work.top, work.right, work.bottom}}
        }};
        auto execution = LayoutExecutor(api).submit(targets);
        if (!execution.submitted) { status->setText(QStringLiteral("选择已失效或扫描未完成，请刷新重试")); return; }
        QTimer::singleShot(180, &owner, [this, execution]() mutable {
            LayoutExecutor(api).observe(execution); QStringList reports;
            for (int i = 0; i < 2; ++i) {
                const auto& report = execution.windows[i];
                reports << QStringLiteral("窗口 %1：%2").arg(i + 1).arg(report.observation == MoveObservation::Reached
                    ? QStringLiteral("已达到目标") : report.observation == MoveObservation::ScanIncomplete
                    ? QStringLiteral("扫描未完成，状态未知") : report.observation == MoveObservation::Missing
                    ? QStringLiteral("窗口已失效") : QStringLiteral("未达到目标，可能不支持调整大小"));
            }
            rescan(); status->setText(reports.join(QStringLiteral("；")));
        });
    }
};
WindowOverview::WindowOverview(QWidget* parent) : QWidget(parent, Qt::Window) {
    setWindowTitle(QStringLiteral("CrossDesk · 窗口总览")); resize(1000, 700); impl_ = std::make_unique<Impl>(*this);
}
WindowOverview::~WindowOverview() = default;
void WindowOverview::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event); if (impl_) QTimer::singleShot(0, this, [this] { impl_->previews(); });
}
void WindowOverview::showEvent(QShowEvent* event) {
    QWidget::showEvent(event); impl_->rescan();
    impl_->animation.stop(); impl_->animation.setTargetObject(this); impl_->animation.setPropertyName("windowOpacity");
    impl_->animation.setStartValue(0.2); impl_->animation.setEndValue(1.0); impl_->animation.setDuration(150); impl_->animation.start();
}
void WindowOverview::hideEvent(QHideEvent* event) { impl_->bridge->clear_previews(); QWidget::hideEvent(event); }
} // namespace crossdesk::desktop
