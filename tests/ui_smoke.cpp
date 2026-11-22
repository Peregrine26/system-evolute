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

// 点某一张卡：真的发鼠标事件，走的是界面里那条点击路径。
// 直接用 cardAt(index)：4 格布局下 index 0..3 就是 4 个格子的层 0 卡片，
// 不再按 y 排序（叠层后 y 序和窗口序不一致）。
void clickCard(crossdesk::ui::CardStack* stack, int index) {
    auto* card = stack->cardAt(index);
    if (!card || !card->isVisible()) {
        return;
    }
    // 点卡片中下部：避开同格后层卡片（后层往右下错开，中下部仍属层 0）。
    const int y = qMax(2, card->height() - 20);
    const QPoint local(card->width() / 2, y);
    const QPoint pos = card->mapTo(stack, local);
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

    // ---- 左栏：四格平铺，4 个窗口各占一格（同格内分层，但 4 个时只有 1 层）----
    check(stack->cardCount() == 4, "左栏有四张窗口卡");
    check(!stack->isStacked(), "四个窗口四格平铺，不叠");
    check(!stack->isCompact(), "四张都完整显示，没有压成标题条");
    check(stack->shadowLayers() == 0, "装饰影子不画（影子是真实卡片，自己画内容）");
    check(stack->shadowSpan() == 0, "四个窗口只有一层，没有错开量");
    check(window.selectedWindow() == windows.first().id, "启动时默认选中第一个窗口");
    check(stack->topWindow() == windows.first().id, "第一张排在最前");

    if (stack->cardAt(0) && stack->cardAt(3)) {
        bool sameSize = true;
        for (int i = 1; i < 4; ++i) {
            sameSize = sameSize && stack->cardAt(i)->size() == stack->cardAt(0)->size();
        }
        check(sameSize, "四张宽高完全一样，占的面积相同");
        check(stack->cardAt(0)->y() >= 12 && stack->cardAt(0)->y() <= 13,
              "第一张从顶部边距开始排（允许余数分配 1px）");
        check(stack->cardAt(3)->geometry().bottom() + 1 >= stack->height() - 16,
              "最后一张接近底部边距，四张基本填满整条栏");
        check(stack->cardAt(0)->geometry().right() + 1 + stack->shadowSpan() ==
                  stack->width() - 12,
              "右边留出了影子的位置，影子不会被裁掉");
        check(stack->cardAt(3)->y() > stack->cardAt(0)->y(), "四张从上往下排");
    }

    // ---- 鼠标碰到卡片：那张卡往上提，移开落回 ----
    if (stack->cardAt(0)) {
        const QRect resting = stack->cardAt(0)->geometry();
        // 鼠标放在卡片身上：那张卡进入提起状态
        const QPoint onCard = resting.center();
        QMouseEvent overCard(QEvent::MouseMove, QPointF(onCard), Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
        QCoreApplication::sendEvent(stack, &overCard);
        QCoreApplication::processEvents();
        check(stack->isLifted(), "鼠标放在卡片身上，那张卡进入提起状态");
        check(waitUntil([&] { return stack->cardAt(0)->y() < resting.y(); }),
              "提起动画把卡片往上抬了");
        check(stack->cardAt(0)->height() == resting.height(), "提起只改 y，卡片高度没变");
        check(stack->cardAt(0)->x() == resting.x(), "提出去的是正上方，没有左右跑");

        // 鼠标移开：落回原位
        const QPoint away(4, 4);
        QMouseEvent leaveCard(QEvent::MouseMove, QPointF(away), Qt::NoButton, Qt::NoButton,
                              Qt::NoModifier);
        QCoreApplication::sendEvent(stack, &leaveCard);
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

    // ---- 四窗口没有叠层：点空白处不会误选 ----
    // （旧的"点影子=点第一张"测试不适用：4 窗口无错开、无影子区域）

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
    check(stack->shadowLayers() == 0, "三张时仍不画装饰影子");
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

    check(stack2->cardCount() == 8, "八个窗口全部传给 CardStack（4 格分组，不截断）");
    check(!stack2->isStacked(), "四格分组，不叠成一列");
    check(stack2->topWindow() == many.windows().first().id, "最前面是第一个窗口");

    // 点第四张卡（slot 3 层 0）能切换选中
    clickCard(stack2, 3);
    const crossdesk::ui::WindowId thirdId = many.windows().at(3).id;
    check(stacked.selectedWindow() == thirdId, "点第四张卡就选中它");
    check(visiblePaneCount(canvas2) == 1, "右栏仍只有一个窗口");
    check(shownWindow(canvas2) == thirdId, "右栏显示的是刚选中那个");

    // ---- CardStack 直接测 4 格分组（绕过 MainWindow 截断）----
    // 8 个窗口：4 格，每格 2 层。第 5-8 叠在第 1-4 后面，往右下错开 10px。
    crossdesk::ui::CardStack stackedOnly;
    stackedOnly.resize(380, 600);
    stackedOnly.setWindows(many.windows());
    stackedOnly.show();
    QCoreApplication::processEvents();
    check(stackedOnly.cardCount() == 8, "CardStack 直接拿到 8 张卡（不截断）");
    check(!stackedOnly.isStacked(), "8 个窗口走 4 格分组，不叠成一列");
    check(stackedOnly.shadowSpan() == 10, "2 层错开 10px");
    // 第 5 张（slot 0, layer 1）应该在第 1 张（slot 0, layer 0）右下方
    if (stackedOnly.cardAt(0) && stackedOnly.cardAt(4)) {
        check(stackedOnly.cardAt(4)->x() > stackedOnly.cardAt(0)->x(),
              "第 5 张在第 1 张右边（同格层 1 往右错开）");
        check(stackedOnly.cardAt(4)->y() > stackedOnly.cardAt(0)->y(),
              "第 5 张在第 1 张下方（同格层 1 往下错开）");
    }

    check(stack2->findChildren<crossdesk::ui::WindowCard*>().size() == 8, "卡片数与窗口数一致，没有重复创建");

    // ---- 窗口更多：超过 4 个时 4 格分组 + 叠层 ----
    crossdesk::ui::FakeService sixteen(nullptr, 12);
    crossdesk::ui::MainWindow crowded(&sixteen);
    crowded.resize(1536, 960);
    crowded.show();
    QCoreApplication::processEvents();

    auto* stack3 = crowded.findChild<crossdesk::ui::CardStack*>(QStringLiteral("cardStack"));
    if (!stack3) {
        std::cout << "FATAL 十二个窗口场景的控件缺失\n";
        return 2;
    }
    check(stack3->cardCount() == 12, "十二个窗口全部传给 CardStack（4 格分组，3 层）");
    check(!stack3->isStacked(), "四格平铺，不叠");
    check(!stack3->isCompact(), "没有压成标题条");

    crossdesk::ui::FakeService lots(nullptr, 12);
    crossdesk::ui::MainWindow crowdest(&lots);
    crowdest.show();
    QCoreApplication::processEvents();

    auto* stack4 = crowdest.findChild<crossdesk::ui::CardStack*>(QStringLiteral("cardStack"));
    auto* canvas4 =
        crowdest.findChild<crossdesk::ui::LayoutPreview*>(QStringLiteral("screenPreview"));
    if (!stack4 || !canvas4) {
        std::cout << "FATAL 十二个窗口场景的控件缺失\n";
        return 2;
    }
    check(stack4->cardCount() == 12, "十二个窗口全部传给 CardStack");
    check(!stack4->isCompact(), "四张平铺，不压成标题条");

    // 点第三张卡能切换选中
    clickCard(stack4, 2);
    const crossdesk::ui::WindowId secondId = lots.windows().at(2).id;
    check(crowdest.selectedWindow() == secondId, "点第三张卡就选中它");
    check(shownWindow(canvas4) == secondId, "右栏跟着换成它");

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
    // 双击层 0 卡片：发出 windowActivated 信号（把真实窗口切到前台）。
    // 4 格布局下点本体命中层 0，不存在旧 stacked 布局的 promote 副作用。
    crossdesk::ui::CardStack doubles;
    doubles.resize(380, 600);
    doubles.setWindows(many.windows()); doubles.show(); QCoreApplication::processEvents();
    auto* first = doubles.cardAt(0);
    const auto original = first->windowId();
    const QPoint click = first->mapTo(&doubles, QPoint(first->width()/2, first->height()/2));
    crossdesk::ui::WindowId activated = 0;
    QObject::connect(&doubles, &crossdesk::ui::CardStack::windowActivated,
                     [&](auto id) { activated = id; });
    mouse(&doubles, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
    mouse(&doubles, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
    mouse(&doubles, QEvent::MouseButtonDblClick, click, Qt::LeftButton, Qt::LeftButton);
    check(activated == original, "双击第一张卡发出 windowActivated 信号");

    crossdesk::ui::FakeService tiles;
    crossdesk::ui::CardStack hover;
    hover.resize(380, 600); hover.setWindows(tiles.windows()); hover.show();
    QCoreApplication::processEvents();
    const QRect resting = hover.cardAt(0)->geometry();
    const QPoint begin = resting.center(), end = begin + QPoint(0,30);
    mouse(&hover, QEvent::MouseButtonPress, begin, Qt::LeftButton, Qt::LeftButton);
    mouse(&hover, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    mouse(&hover, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    // 拖放后鼠标移到卡片身上，仍会提起
    mouse(&hover, QEvent::MouseMove, resting.center(), Qt::NoButton, Qt::NoButton);
    check(waitUntil([&] { return hover.cardAt(0)->y() < resting.y(); }), "拖放后悬停仍会提起");
    QElapsedTimer settled; settled.start();
    waitUntil([&] { return settled.elapsed() >= 250; });
    check(hover.isLifted() && hover.cardAt(0)->y() < resting.y(),
          "回位动画结束后不会覆盖提起结果");

    std::cout << "\n通过 " << g_passed << " 项，失败 " << g_failed << " 项\n";
    return g_failed == 0 ? 0 : 1;
}
