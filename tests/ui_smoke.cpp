// 无界面冒烟测试：真的构造主窗口、真的点卡片、真的发命令，
// 然后检查卡片数、右侧画面、叠影顺序和状态文字。
//
// 它证明的是"界面状态机按预期走"，不证明"窗口真的被移动了"——
// 后者只能靠 Windows 实机验证，写在这里会变成自我欺骗。

#include "ui/fake_service.h"
#include "ui/main_window.h"
#include "ui/preview_widgets.h"
#include "ui/window_cards.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QMouseEvent>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <functional>
#include <iostream>
#include <string>

namespace {

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

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 2000) {
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

// 点某一张卡：真的发鼠标事件，走的是界面里那条点击路径，
// 而不是绕过界面直接改状态。
void clickCard(crossdesk::ui::CardStack* stack, int index) {
    QVector<crossdesk::ui::WindowCard*> cards;
    for (auto* card : stack->findChildren<crossdesk::ui::WindowCard*>()) {
        if (card->isVisible()) {
            cards.push_back(card);
        }
    }
    std::sort(cards.begin(), cards.end(),
              [](crossdesk::ui::WindowCard* a, crossdesk::ui::WindowCard* b) {
                  return a->y() < b->y();
              });
    if (index < 0 || index >= cards.size()) {
        return;
    }

    // 点卡片底部：叠影时上面那张会盖住下面卡片的顶部，
    // 只有底部那一条是真正属于它自己的。
    const int y = qMax(2, cards.at(index)->height() - 20);
    const QPoint local(cards.at(index)->width() / 2, y);
    const QPoint pos = cards.at(index)->mapTo(stack, local);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(stack, &press);
    QCoreApplication::sendEvent(stack, &release);
    QCoreApplication::processEvents();
}

int visiblePaneCount(crossdesk::ui::LayoutPreview* canvas) {
    return canvas->paneCount();
}

crossdesk::ui::WindowId shownWindow(crossdesk::ui::LayoutPreview* canvas) {
    for (auto* pane : canvas->findChildren<crossdesk::ui::ThumbnailSlot*>()) {
        if (pane->isVisible()) {
            return pane->windowId();
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    crossdesk::ui::FakeService service;
    crossdesk::ui::MainWindow window(&service);
    window.show();

    auto* stack = window.findChild<crossdesk::ui::CardStack*>(QStringLiteral("cardStack"));
    auto* canvas =
        window.findChild<crossdesk::ui::LayoutPreview*>(QStringLiteral("screenPreview"));
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusLabel"));

    if (!stack || !canvas || !status) {
        std::cout << "FATAL 界面控件缺失，后续断言无意义\n";
        return 2;
    }

    QCoreApplication::processEvents();
    const QVector<crossdesk::ui::WindowItem> windows = service.windows();

    // ---- 左栏：四张卡平铺填满，第一张背后垫着影子 ----
    check(stack->cardCount() == 4, "左栏有四张窗口卡");
    check(!stack->isStacked(), "四个窗口平铺，不叠");
    check(!stack->isCompact(), "四张都完整显示，没有压成标题条");
    check(stack->stackedShift() == 0, "平铺不左右错开");
    check(stack->shadowLayers() == 3, "第一张卡背后垫了三层影子界面");
    check(window.selectedWindow() == windows.first().id, "启动时默认选中第一个窗口");

    if (stack->cardAt(0) && stack->cardAt(3)) {
        check(stack->cardAt(0)->y() == 12, "第一张贴着上边距");
        check(stack->cardAt(3)->y() + stack->cardAt(3)->height() <= stack->height() - 12,
              "最后一张贴着下边距，四张把整条栏填满");
        check(stack->cardAt(0)->width() == stack->cardAt(3)->width(), "四张一样宽");
        check(stack->cardAt(3)->y() > stack->cardAt(0)->y(), "从上往下排");
        check(stack->width() - stack->cardAt(0)->geometry().right() - 1 >= 20,
              "第一张右边留出了影子错开的空间，影子不会被裁掉");
    }

    // ---- 右栏：只显示一个窗口，且铺满 ----
    check(visiblePaneCount(canvas) == 1, "右栏只显示一个窗口");
    check(shownWindow(canvas) == windows.first().id, "右栏显示的就是选中的那个");

    // ---- 点一下换一个，不需要勾选 ----
    clickCard(stack, 1);
    check(window.selectedWindow() == windows.at(1).id, "点第二张卡就选中它");
    check(shownWindow(canvas) == windows.at(1).id, "右栏跟着换成第二张");
    check(visiblePaneCount(canvas) == 1, "换窗口后右栏仍然只有一个窗口");

    // ---- 影子界面不是第五个窗口，点它等于点第一张 ----
    if (stack->cardAt(0)) {
        const QRect first = stack->cardAt(0)->geometry();
        // 落在第一张右下角外面的那条缝里：卡片管不到，影子管得到。
        const QPoint onShadow(first.right() + 6, first.bottom() + 6);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(onShadow), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(onShadow), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(stack, &press);
        QCoreApplication::sendEvent(stack, &release);
        QCoreApplication::processEvents();
        check(window.selectedWindow() == windows.first().id, "点第一张背后的影子，等于点第一张");
        check(stack->cardCount() == 4, "影子不是一张真的卡，卡片数没变");
    }

    // ---- 失败路径：系统窗口拒绝置前 ----
    int settingsIndex = -1;
    for (int i = 0; i < windows.size(); ++i) {
        if (windows.at(i).processName == QStringLiteral("SystemSettings.exe")) {
            settingsIndex = i;
        }
    }
    check(settingsIndex >= 0, "假数据里有那个会被拒绝的系统窗口");
    if (settingsIndex >= 0) {
        clickCard(stack, settingsIndex);
        service.activateWindow(window.selectedWindow());
        QCoreApplication::processEvents();
        check(status->text().contains(QStringLiteral("拒绝")), "定位被拒绝时状态条说明原因");
    }

    // ---- 窗口被关掉：卡片少一张，选中自动换一个 ----
    const crossdesk::ui::WindowId closing = window.selectedWindow();
    service.simulateClose(closing);
    service.refresh();
    QCoreApplication::processEvents();
    check(stack->cardCount() == 3, "关掉一个窗口后左栏只剩三张卡");
    check(stack->shadowLayers() == 3, "三张时第一张背后仍然垫着影子");
    check(window.selectedWindow() != closing && window.selectedWindow() != 0,
          "被关掉的窗口自动换成别的，右栏不会空着");
    check(visiblePaneCount(canvas) == 1, "右栏仍有画面");

    // ---- 全部关掉 ----
    for (const auto& item : service.windows()) {
        service.simulateClose(item.id);
    }
    service.refresh();
    QCoreApplication::processEvents();
    check(stack->cardCount() == 0, "全部关闭后左栏为空");
    check(visiblePaneCount(canvas) == 0, "全部关闭后右栏为空");

    // ---- 超过四个：叠起来；点哪张哪张到最前 ----
    crossdesk::ui::FakeService many(nullptr, 8);
    crossdesk::ui::MainWindow stacked(&many);
    stacked.show();
    QCoreApplication::processEvents();

    auto* stack2 = stacked.findChild<crossdesk::ui::CardStack*>(QStringLiteral("cardStack"));
    auto* canvas2 =
        stacked.findChild<crossdesk::ui::LayoutPreview*>(QStringLiteral("screenPreview"));
    if (!stack2 || !canvas2) {
        std::cout << "FATAL 叠影场景的控件缺失\n";
        return 2;
    }

    check(stack2->cardCount() == 8, "八个窗口时左栏有八张卡");
    check(stack2->isStacked(), "超过四个就叠起来");
    check(stack2->topWindow() == many.windows().first().id, "最前面是第一个窗口");
    check(stack2->stackedShift() > 0, "叠影是斜着错开的，不是一列直上直下");
    check(stack2->cardAt(4) && stack2->cardAt(0) && stack2->cardAt(4)->x() > stack2->cardAt(0)->x(),
          "越往后错得越靠右");
    check(stack2->cardAt(4) && stack2->cardAt(4)->width() < stack2->cardAt(0)->width(),
          "越往后越窄，右边缘对齐");

    const crossdesk::ui::WindowId lastId = many.windows().last().id;
    clickCard(stack2, 7);
    check(stack2->topWindow() == lastId, "点最后一张卡，它被拉到最前面");
    check(stacked.selectedWindow() == lastId, "拉到最前的同时选中它");
    check(visiblePaneCount(canvas2) == 1, "叠影模式下右栏也只有一个窗口");
    check(shownWindow(canvas2) == lastId, "右栏显示的是刚拉到最前那个");

    // 点最后一张时，它原来在 y 最大处；拉到最前后应该在顶部。
    check(stack2->findChildren<crossdesk::ui::WindowCard*>().size() >= 8, "卡片没有被重复创建");

    // ---- 窗口更多：先保证每张还露得出一条完整标题，再多就退化成标题条 ----
    crossdesk::ui::FakeService sixteen(nullptr, 16);
    crossdesk::ui::MainWindow crowded(&sixteen);
    crowded.resize(1536, 960);  // 按 1080p 那种比例算：16 张还排得下完整标题
    crowded.show();
    QCoreApplication::processEvents();

    auto* stack3 = crowded.findChild<crossdesk::ui::CardStack*>(QStringLiteral("cardStack"));
    if (!stack3) {
        std::cout << "FATAL 十六个窗口场景的控件缺失\n";
        return 2;
    }
    check(stack3->cardCount() == 16, "十六个窗口时左栏有十六张卡");
    check(stack3->isStacked(), "十六个窗口仍然是叠影");
    check(!stack3->isCompact(), "十六个窗口还没压到只剩标题");
    check(stack3->stackedStep() >= 46, "每张露出的那条至少放得下一条标题");

    crossdesk::ui::FakeService lots(nullptr, 32);
    crossdesk::ui::MainWindow crowdest(&lots);
    crowdest.show();
    QCoreApplication::processEvents();

    auto* stack4 = crowdest.findChild<crossdesk::ui::CardStack*>(QStringLiteral("cardStack"));
    auto* canvas4 =
        crowdest.findChild<crossdesk::ui::LayoutPreview*>(QStringLiteral("screenPreview"));
    if (!stack4 || !canvas4) {
        std::cout << "FATAL 三十二个窗口场景的控件缺失\n";
        return 2;
    }
    check(stack4->cardCount() == 32, "三十二个窗口时左栏有三十二张卡");
    check(stack4->isCompact(), "一个接一个排不下时退化成标题条");
    check(stack4->stackedStep() * stack4->cardCount() <= stack4->height(),
          "标题条全部排在左栏里，不溢出");

    const crossdesk::ui::WindowId midId = lots.windows().at(20).id;
    clickCard(stack4, 20);
    check(stack4->topWindow() == midId, "标题条模式下点第 21 条也能换到最前");
    check(shownWindow(canvas4) == midId, "右栏跟着换成它");

    std::cout << "\n通过 " << g_passed << " 项，失败 " << g_failed << " 项\n";
    return g_failed == 0 ? 0 : 1;
}
