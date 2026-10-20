// 无显示器冒烟测试：真的构造主窗口、真的勾选、真的发命令，
// 然后检查列表行数、画面块数、排列比例和状态文字。
//
// 它证明的是"界面状态机按预期走"，不证明"窗口真的被移动了"——
// 后者只能靠 Windows 实机验证，写在这里会变成自我欺骗。

#include "ui/fake_service.h"
#include "ui/main_window.h"
#include "ui/preview_widgets.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QRadioButton>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <functional>
#include <iostream>
#include <string>

namespace {

constexpr int kWindowIdRole = Qt::UserRole + 1;
int g_failed = 0;
int g_passed = 0;

void check(bool condition, const std::string& what) {
    if (condition) {
        ++g_passed;
        std::cout << "PASS " << what << "\n";
    } else {
        ++g_failed;
        std::cout << "FAIL " << what << "\n";
    }
}

int rowCount(QListWidget* list) {
    int count = 0;
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(kWindowIdRole).isValid()) {
            ++count;
        }
    }
    return count;
}

int checkedCount(QListWidget* list) {
    int count = 0;
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        if (item->data(kWindowIdRole).isValid() && item->checkState() == Qt::Checked) {
            ++count;
        }
    }
    return count;
}

QListWidgetItem* rowWithTitle(QListWidget* list, const QString& title) {
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        if (item->data(kWindowIdRole).isValid() && item->text().contains(title)) {
            return item;
        }
    }
    return nullptr;
}

void uncheckAll(QListWidget* list) {
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        if (item->data(kWindowIdRole).isValid() && item->checkState() == Qt::Checked) {
            item->setCheckState(Qt::Unchecked);
        }
    }
}

void checkOnly(QListWidget* list, const QStringList& titles) {
    uncheckAll(list);
    for (const QString& title : titles) {
        if (QListWidgetItem* item = rowWithTitle(list, title)) {
            item->setCheckState(Qt::Checked);
        }
    }
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return predicate();
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    crossdesk::ui::FakeService service;
    crossdesk::ui::MainWindow window(&service);
    window.show();

    auto* list = window.findChild<QListWidget*>(QStringLiteral("windowList"));
    auto* activate = window.findChild<QPushButton*>(QStringLiteral("activateButton"));
    auto* confirm = window.findChild<QPushButton*>(QStringLiteral("confirmButton"));
    auto* back = window.findChild<QPushButton*>(QStringLiteral("backButton"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusLabel"));
    auto* equal = window.findChild<QRadioButton*>(QStringLiteral("modeEqual"));
    auto* master = window.findChild<QRadioButton*>(QStringLiteral("modeLeftMaster"));
    auto* canvas = window.findChild<crossdesk::ui::LayoutPreview*>(QStringLiteral("screenPreview"));

    if (!list || !activate || !confirm || !back || !status || !equal || !master || !canvas) {
        std::cout << "FATAL 界面控件缺失，后续断言无意义\n";
        return 2;
    }

    // 换画面时的淡出淡入期间会禁用操作按钮。测试里所有"等画面切完"的地方
    // 都要等到动画真正结束，否则会误判成"按钮该可用却不可用"。
    const auto waitIdle = [&window] {
        return waitUntil([&window] { return !window.isAnimating(); });
    };

    // ---- M1：列表 ----
    check(rowCount(list) == 4, "任务栏列出 4 个窗口（含系统应用窗口「设置」）");
    check(rowWithTitle(list, QStringLiteral("设置")) != nullptr, "系统应用窗口也能列出");
    check(rowWithTitle(list, QStringLiteral("explorer.exe")) != nullptr, "列表里能看到进程名");

    // ---- 桌面画面：所有窗口都画出来，勾选只是加高亮 ----
    check(!window.isArranging(), "启动后是桌面画面，不是排列画面");
    check(canvas->paneCount() == 4, "桌面画面上摆着全部 4 个窗口（按真实位置重叠）");
    check(!activate->isVisible(), "没勾选时不显示任何操作按钮");

    checkOnly(list, {QStringLiteral("未命名 - 记事本")});
    check(checkedCount(list) == 1, "勾选一个窗口");
    check(activate->isVisible() && activate->isEnabled(), "勾一个：出现「定位」");
    check(canvas->paneCount() == 4, "勾一个不改变桌面画面，只是高亮那一块");

    // ---- 勾满两个：自动切到排列画面 ----
    checkOnly(list, {QStringLiteral("未命名 - 记事本"), QStringLiteral("项目资料")});
    check(checkedCount(list) == 2, "勾选两个窗口");
    check(waitUntil([&window] { return window.isArranging(); }), "勾满两个自动进入排列画面");
    waitIdle();
    check(canvas->paneCount() == 2, "排列画面上只剩要排的两个窗口");
    check(confirm->isVisible() && confirm->isEnabled(), "画面上「确认排列」可用");
    check(equal->isVisible() && master->isVisible(), "画面上出现等宽 / 60-40 比例选项");

    // 只取真正显示出来的两块，并按横坐标区分左右。
    // 不能直接用 findChildren 的顺序：raise() 会重排父控件的 children 列表。
    const auto visiblePanes = [canvas] {
        QVector<crossdesk::ui::ThumbnailSlot*> found;
        for (auto* pane : canvas->findChildren<crossdesk::ui::ThumbnailSlot*>()) {
            if (pane->isVisible()) {
                found.push_back(pane);
            }
        }
        std::sort(found.begin(), found.end(),
                  [](crossdesk::ui::ThumbnailSlot* a, crossdesk::ui::ThumbnailSlot* b) {
                      return a->x() < b->x();
                  });
        return found;
    };

    QCoreApplication::processEvents();
    QVector<crossdesk::ui::ThumbnailSlot*> panes = visiblePanes();
    check(panes.size() == 2, "画布上正好两块");
    if (panes.size() == 2) {
        equal->setChecked(true);
        QCoreApplication::processEvents();
        panes = visiblePanes();
        check(qAbs(panes.at(0)->width() - panes.at(1)->width()) <= 8,
              "等宽模式下左右两块宽度基本一致");

        master->setChecked(true);
        QCoreApplication::processEvents();
        panes = visiblePanes();
        const double ratio =
            static_cast<double>(panes.at(0)->width()) / qMax(1, panes.at(1)->width());
        check(qAbs(ratio - 1.5) < 0.08, "60/40 模式下左块宽度约为右块的 1.5 倍");
    }

    // ---- M3：确认排列 ----
    const QVector<crossdesk::ui::WindowItem> before = service.windows();
    confirm->click();
    check(waitUntil([&window] { return !window.isArranging(); }), "确认后回到桌面画面");
    waitIdle();
    check(status->text().contains(QStringLiteral("排列完成")), "状态条报告排列完成");
    check(canvas->paneCount() == 4, "回到桌面画面后所有窗口又都在了");

    const QVector<crossdesk::ui::WindowItem> after = service.windows();
    bool boundsChanged = false;
    for (const auto& item : after) {
        for (const auto& old : before) {
            if (item.id == old.id && item.bounds != old.bounds) {
                boundsChanged = true;
            }
        }
    }
    check(boundsChanged, "窗口的目标位置真的变了（不是只在界面上画画）");

    // ---- M2：定位的失败路径 ----
    checkOnly(list, {QStringLiteral("设置")});
    QCoreApplication::processEvents();
    check(activate->isVisible(), "只勾一个时「定位」还在");
    activate->click();
    QCoreApplication::processEvents();
    check(status->text().contains(QStringLiteral("拒绝")), "受限窗口定位失败并说明原因");
    if (QListWidgetItem* settings = rowWithTitle(list, QStringLiteral("设置"))) {
        check(settings->text().contains(QStringLiteral("!")), "失败原因标记在对应窗口那一行");
    }

    // ---- 空态与窗口消失 ----
    checkOnly(list, {QStringLiteral("未命名 - 记事本")});
    service.simulateClose(1);
    service.refresh();  // 等价于 Ctrl+R：走的是同一条 windowsChanged 路径
    QCoreApplication::processEvents();
    check(rowCount(list) == 3, "关闭窗口后刷新，旧条目消失");
    check(checkedCount(list) == 0, "被关闭的窗口自动从勾选中移除");
    check(status->text().contains(QStringLiteral("关闭")), "状态条说明有窗口已关闭");
    check(canvas->paneCount() == 3, "画面同步少了一块");

    // 全部关掉后的空态
    for (const auto& item : service.windows()) {
        service.simulateClose(item.id);
    }
    service.refresh();
    QCoreApplication::processEvents();
    check(rowCount(list) == 0, "全部关闭后列表为空");
    check(list->count() == 1 && !list->item(0)->data(kWindowIdRole).isValid(),
          "空列表显示占位提示而不是一片空白");
    check(canvas->paneCount() == 0, "全部关闭后画面为空");

    // ---- Esc 与返回 ----
    service.reset();
    service.refresh();
    QCoreApplication::processEvents();
    checkOnly(list, {QStringLiteral("未命名 - 记事本"), QStringLiteral("项目资料")});
    check(waitUntil([&window] { return window.isArranging(); }), "再次进入排列画面");
    waitIdle();
    back->click();
    check(waitUntil([&window] { return !window.isArranging(); }), "「返回」回到桌面画面");

    std::cout << "\n通过 " << g_passed << " 项，失败 " << g_failed << " 项\n";
    return g_failed == 0 ? 0 : 1;
}
