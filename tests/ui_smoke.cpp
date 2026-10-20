// 无显示器冒烟测试：真的构造主窗口、真的勾选、真的发命令，
// 然后检查列表行数、按钮可用性、预览几何和状态栏文字。
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
#include <QStackedWidget>
#include <QThread>

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
    auto* pages = window.findChild<QStackedWidget*>(QStringLiteral("pages"));
    auto* activate = window.findChild<QPushButton*>(QStringLiteral("activateButton"));
    auto* arrange = window.findChild<QPushButton*>(QStringLiteral("arrangeButton"));
    auto* confirm = window.findChild<QPushButton*>(QStringLiteral("confirmButton"));
    auto* back = window.findChild<QPushButton*>(QStringLiteral("backButton"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusLabel"));
    auto* refresh = window.findChild<QPushButton*>(QStringLiteral("refreshButton"));
    auto* equal = window.findChild<QRadioButton*>(QStringLiteral("modeEqual"));
    auto* master = window.findChild<QRadioButton*>(QStringLiteral("modeLeftMaster"));
    auto* canvas = window.findChild<crossdesk::ui::LayoutPreview*>(QStringLiteral("previewCanvas"));

    if (!list || !pages || !activate || !arrange || !confirm || !back || !status || !equal ||
        !master || !canvas || !refresh) {
        std::cout << "FATAL 界面控件缺失，后续断言无意义\n";
        return 2;
    }

    // 换页动画期间窗口会禁用操作按钮。测试里所有"等页面切完"的地方都要等到
    // 动画真正结束，否则会误判成"按钮该可用却不可用"。
    const auto waitIdle = [refresh] { return waitUntil([refresh] { return refresh->isEnabled(); }); };

    // ---- M1：列表与刷新 ----
    check(rowCount(list) == 4, "总览列出 4 个窗口（含系统应用窗口「设置」）");
    check(rowWithTitle(list, QStringLiteral("设置")) != nullptr, "系统应用窗口也能列出");
    check(rowWithTitle(list, QStringLiteral("explorer.exe")) != nullptr, "列表里能看到进程名");

    // ---- 两窗选择 ----
    check(!activate->isEnabled() && !arrange->isEnabled(), "未勾选时两个操作都不可用");

    checkOnly(list, {QStringLiteral("未命名 - 记事本")});
    check(checkedCount(list) == 1, "勾选一个窗口");
    check(activate->isEnabled() && !arrange->isEnabled(), "勾一个：定位可用、排列不可用");

    checkOnly(list, {QStringLiteral("未命名 - 记事本"), QStringLiteral("项目资料")});
    check(checkedCount(list) == 2, "勾选两个窗口");
    check(arrange->isEnabled(), "勾两个：排列可用");

    // 勾第三个应当替换最早的，而不是静默失败
    checkOnly(list, {QStringLiteral("未命名 - 记事本"), QStringLiteral("项目资料")});
    if (QListWidgetItem* third = rowWithTitle(list, QStringLiteral("设置"))) {
        third->setCheckState(Qt::Checked);
    }
    check(checkedCount(list) == 2, "勾第三个后仍然只有两个被选中");
    check(status->text().contains(QStringLiteral("替换")), "状态栏说明发生了替换");

    // ---- M3：进入布局预览 ----
    checkOnly(list, {QStringLiteral("未命名 - 记事本"), QStringLiteral("项目资料")});
    arrange->click();
    check(waitUntil([pages] { return pages->currentIndex() == 1; }), "点「排列」进入预览页");
    // 换页动画期间按钮是禁用的（防止连点发两条命令），所以要等动画收尾。
    check(waitUntil([confirm] { return confirm->isEnabled(); }),
          "动画结束后「确认排列」可用");

    // 注意变量名：slots 是 Qt 的空宏，不能拿来当标识符。
    const auto panes = canvas->findChildren<crossdesk::ui::ThumbnailSlot*>();
    check(panes.size() == 2, "预览画布上有两个窗口位置");

    // 等布局完成再比较宽度
    QCoreApplication::processEvents();
    equal->setChecked(true);
    QCoreApplication::processEvents();
    const int equalLeft = panes.at(0)->width();
    const int equalRight = panes.at(1)->width();
    check(qAbs(equalLeft - equalRight) <= 8, "等宽模式下左右两块宽度基本一致");

    master->setChecked(true);
    QCoreApplication::processEvents();
    check(panes.at(0)->width() > panes.at(1)->width() * 3 / 2,
          "60/40 模式下左块明显更宽");

    // ---- M3：确认排列 ----
    const QVector<crossdesk::ui::WindowItem> before = service.windows();
    confirm->click();
    check(waitUntil([pages] { return pages->currentIndex() == 0; }), "确认后回到总览页");
    waitIdle();
    check(status->text().contains(QStringLiteral("排列完成")), "状态栏报告排列完成");

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
    activate->click();
    QCoreApplication::processEvents();
    check(status->text().contains(QStringLiteral("拒绝")), "受限窗口定位失败并说明原因");
    if (QListWidgetItem* settings = rowWithTitle(list, QStringLiteral("设置"))) {
        check(settings->text().contains(QStringLiteral("!")), "失败原因标记在对应窗口那一行");
    }

    // ---- 空态与窗口消失 ----
    checkOnly(list, {QStringLiteral("未命名 - 记事本")});
    service.simulateClose(1);
    refresh->click();
    QCoreApplication::processEvents();
    check(rowCount(list) == 3, "关闭窗口后刷新，旧条目消失");    check(checkedCount(list) == 0, "被关闭的窗口自动从勾选中移除");
    check(status->text().contains(QStringLiteral("关闭")), "状态栏说明有窗口已关闭");

    // 全部关掉后的空态
    for (const auto& item : service.windows()) {
        service.simulateClose(item.id);
    }
    refresh->click();
    QCoreApplication::processEvents();
    check(rowCount(list) == 0, "全部关闭后列表为空");
    check(list->count() == 1 && !list->item(0)->data(kWindowIdRole).isValid(),
          "空列表显示占位提示而不是一片空白");

    // ---- Esc 与页面返回 ----
    service.reset();
    refresh->click();
    QCoreApplication::processEvents();
    checkOnly(list, {QStringLiteral("未命名 - 记事本"), QStringLiteral("项目资料")});
    arrange->click();
    check(waitUntil([pages] { return pages->currentIndex() == 1; }), "再次进入预览页");
    waitIdle();
    back->click();
    check(waitUntil([pages] { return pages->currentIndex() == 0; }), "「返回」回到总览页");

    std::cout << "\n通过 " << g_passed << " 项，失败 " << g_failed << " 项\n";
    return g_failed == 0 ? 0 : 1;
}
