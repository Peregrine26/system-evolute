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

    // ---- 左栏：四张平铺填满，只有第一张背后垫着影子 ----
    check(stack->cardCount() == 4, "左栏有四张窗口卡");
    check(!stack->isStacked(), "四个窗口平铺，不叠");
    check(!stack->isCompact(), "四张都完整显示，没有压成标题条");
    check(stack->shadowLayers() == 3, "只有第一张卡背后垫了三层影子界面");
    check(stack->shadowSpan() == 30, "影子往右下共错开 30px");
    check(window.selectedWindow() == windows.first().id, "启动时默认选中第一个窗口");
    check(stack->topWindow() == windows.first().id, "第一张排在最前");

    if (stack->cardAt(0) && stack->cardAt(3)) {
        bool sameSize = true;
        for (int i = 1; i < 4; ++i) {
            sameSize = sameSize && stack->cardAt(i)->size() == stack->cardAt(0)->size();
        }
        check(sameSize, "四张宽高完全一样，占的面积相同");
        check(stack->cardAt(0)->y() == 12, "第一张也从顶部边距开始排，不再为影子让位");
        check(stack->cardAt(3)->geometry().bottom() + 1 == stack->height() - 12,
              "最后一张贴着下边距，四张把整条栏填满");
        check(stack->cardAt(0)->geometry().right() + 1 + stack->shadowSpan() ==
                  stack->width() - 12,
              "右边留出了影子的位置，影子不会被裁掉");
        check(stack->cardAt(3)->y() > stack->cardAt(0)->y(), "四张从上往下排");
    }

    // ---- 鼠标压到外露的影子上：第一张往上提，移开落回去 ----
    if (stack->cardAt(0)) {
        const QRect resting = stack->cardAt(0)->geometry();
        const QPoint onCard = resting.center();
        QMouseEvent overCard(QEvent::MouseMove, QPointF(onCard), Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
        QCoreApplication::sendEvent(stack, &overCard);
        QCoreApplication::processEvents();
        check(stack->cardAt(0)->y() == resting.y(), "鼠标只是放在卡片身上，不会提起来");

        const QPoint onEdge(resting.right() + 12, resting.y() + resting.height() / 2);
        QMouseEvent overEdge(QEvent::MouseMove, QPointF(onEdge), Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
        QCoreApplication::sendEvent(stack, &overEdge);
        check(stack->isLifted(), "鼠标压到外露的框上，第一张进入提起状态");
        check(waitUntil([&] { return stack->cardAt(0)->y() == resting.y() - 10; }),
              "提起动画把第一张往上抬了 10px");
        check(stack->cardAt(0)->height() == resting.height(), "提起只改 y，卡片高度没变");
        check(stack->cardAt(0)->x() == resting.x(), "提出去的是正上方，没有左右跑");

        const QPoint away(4, 4);
        QMouseEvent leaveEdge(QEvent::MouseMove, QPointF(away), Qt::NoButton, Qt::NoButton,
                              Qt::NoModifier);
        QCoreApplication::sendEvent(stack, &leaveEdge);
        check(!stack->isLifted(), "鼠标移开，提起状态结束");
        check(waitUntil([&] { return stack->cardAt(0)->y() == resting.y(); }),
              "落回原来的位置，四张又对齐了");
    }

    // ---- 右栏：只显示一个窗口，且铺满 ----
    check(visiblePaneCount(canvas) == 1, "右栏只显示一个窗口");
    check(shownWindow(canvas) == windows.first().id, "右栏显示的就是选中的那个");

    // ---- 点一下换一个，不需要勾选 ----
    clickCard(stack, 1);
    check(window.selectedWindow() == windows.at(1).id, "点第二张卡就选中它");
    check(shownWindow(canvas) == windows.at(1).id, "右栏跟着换成第二张");
    check(visiblePaneCount(canvas) == 1, "换窗口后右栏仍然只有一个窗口");

    // ---- 影子不是一张卡：点它等于点第一张 ----
    if (stack->cardAt(0)) {
        const QRect first = stack->cardAt(0)->geometry();
        // 落在第一张右边外露的那条影子上：卡片管不到，影子管得到。
        const QPoint onShadow(first.right() + 12, first.y() + 20);
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

    // A receives a top-level HWND and client content coordinates, with no child HWND creation.
    if (stack3->cardAt(0)) {
        auto* card = stack3->cardAt(0);
        const auto childId = card->internalWinId();
        check(card->hostHandle() == static_cast<quintptr>(crowded.winId()), "卡片宿主是顶层窗口");
        check(card->internalWinId() == childId, "查询卡片宿主不创建子 HWND");
        check(card->previewRect() == QRect(card->mapTo(&crowded, card->contentRect().topLeft()),
                                          card->contentRect().size()), "卡片预览使用客户区内容坐标");
    }
    for (auto* pane : canvas4->findChildren<crossdesk::ui::ThumbnailSlot*>()) {
        if (!pane->isVisible()) continue;
        const auto childId = pane->internalWinId();
        check(pane->hostHandle() == static_cast<quintptr>(crowdest.winId()), "右侧预览宿主是顶层窗口");
        check(pane->internalWinId() == childId, "查询预览宿主不创建子 HWND");
        check(pane->previewRect() == QRect(pane->mapTo(&crowdest, pane->contentRect().topLeft()),
                                          pane->contentRect().size()), "预览矩形排除标题栏并映射到客户区");
    }
    const auto mouse = [](QWidget* target, QEvent::Type type, const QPoint& pos,
                          Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, QPointF(pos), QPointF(target->mapToGlobal(pos)), button, buttons,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(target, &event);
    };
    // The first click promotes the last card before Qt delivers the double click.
    crossdesk::ui::CardStack doubles;
    doubles.resize(380, 600);
    doubles.setWindows(many.windows()); doubles.show(); QCoreApplication::processEvents();
    auto* last = doubles.cardAt(7);
    const auto original = last->windowId();
    const QPoint click = last->mapTo(&doubles, QPoint(last->width()/2, last->height()-20));
    crossdesk::ui::WindowId activated = 0;
    QObject::connect(&doubles, &crossdesk::ui::CardStack::windowActivated,
                     [&](auto id) { activated = id; });
    mouse(&doubles, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
    mouse(&doubles, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
    waitUntil([&] { return doubles.cardAt(0)->y() == 12; });
    mouse(&doubles, QEvent::MouseButtonDblClick, click, Qt::LeftButton, Qt::LeftButton);
    check(activated == original, "重排后双击仍激活第一次点击的窗口");

    crossdesk::ui::FakeService tiles;
    crossdesk::ui::CardStack hover;
    hover.resize(380, 600); hover.setWindows(tiles.windows()); hover.show();
    QCoreApplication::processEvents();
    const QRect resting = hover.cardAt(0)->geometry();
    const QPoint begin = resting.center(), end = begin + QPoint(0,30);
    mouse(&hover, QEvent::MouseButtonPress, begin, Qt::LeftButton, Qt::LeftButton);
    mouse(&hover, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(&hover, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    mouse(&hover, QEvent::MouseMove, QPoint(resting.right()+12, resting.y()+20), Qt::NoButton, Qt::NoButton);
    check(waitUntil([&] { return hover.cardAt(0)->y() == resting.y()-10; }), "拖放后立即悬停仍会提起");
    QElapsedTimer settled; settled.start();
    waitUntil([&] { return settled.elapsed() >= 250; });
    check(hover.isLifted() && hover.cardAt(0)->y() == resting.y()-10,
          "回位动画结束后不会覆盖提起结果");

    std::cout << "\n通过 " << g_passed << " 项，失败 " << g_failed << " 项\n";
    return g_failed == 0 ? 0 : 1;
}
