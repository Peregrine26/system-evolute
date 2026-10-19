// 无界面冒烟测试：把 OverviewWindow 放到 offscreen 平台上跑起来，
// 用代码模拟"切工作区 / 移动 / 窗口被关闭 / 清空 / 重置"，
// 检查左侧列表是否随之变化。
//
// 它证明的是"界面和 service 的状态机接对了"，不证明"好不好看"。
// 有显示器时的手工验收仍然要做，这两者不能互相替代。

#include "ui/fake_workspace_service.h"
#include "ui/overview_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVariant>

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

// 队列连接投递的信号要等事件循环。多转两圈，避免"信号还在队列里就断言"。
void pump(QApplication& app) {
    QCoreApplication::sendPostedEvents();
    app.processEvents();
    QCoreApplication::sendPostedEvents();
    app.processEvents();
}

// 可被选中的行 = 真正的窗口行。分组表头和空态占位项都被设成 NoItemFlags。
int windowRows(QListWidget* list) {
    int count = 0;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->flags() != Qt::NoItemFlags) {
            ++count;
        }
    }
    return count;
}

QString rowText(QListWidget* list, int index) {
    QListWidgetItem* item = list->item(index);
    return item ? item->text() : QString();
}

QPushButton* findButton(QWidget& root, const QString& text) {
    for (QPushButton* button : root.findChildren<QPushButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

QLabel* findLabelStartingWith(QWidget& root, const QString& prefix) {
    for (QLabel* label : root.findChildren<QLabel*>()) {
        if (label->text().startsWith(prefix)) {
            return label;
        }
    }
    return nullptr;
}

crossdesk::ui::WindowId currentRowId(QListWidget* list) {
    if (!list->currentItem()) {
        return 0;
    }
    return static_cast<crossdesk::ui::WindowId>(
        list->currentItem()->data(Qt::UserRole + 1).toULongLong());
}

}  // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    crossdesk::ui::FakeWorkspaceService service;
    crossdesk::ui::OverviewWindow window(&service);
    window.show();
    pump(app);

    auto* list = window.findChild<QListWidget*>();
    check(list != nullptr, "找到左侧窗口列表");
    if (!list) {
        return 1;
    }

    QLabel* status = findLabelStartingWith(window, QStringLiteral("当前："));
    check(status != nullptr, "找到底部状态栏");

    // 1. 初始状态：基础桌面 3 个窗口
    check(list->count() == 3 && windowRows(list) == 3, "初始列表显示 3 个窗口");
    check(rowText(list, 0).contains(QStringLiteral("Chrome")), "第一行是 Chrome");

    // 2. 切到"学习"：整组换掉
    service.switchWorkspace(QStringLiteral("study"));
    pump(app);
    check(windowRows(list) == 3 && rowText(list, 0).contains(QStringLiteral("VS Code")),
          "切到学习后列表整组换掉");

    // 3. 查看所有窗口：6 个窗口 + 2 个分组表头
    QPushButton* showAll = findButton(window, QStringLiteral("查看所有窗口"));
    check(showAll != nullptr, "找到「查看所有窗口」按钮");
    if (showAll) {
        showAll->setChecked(true);
        pump(app);
        check(windowRows(list) == 6 && list->count() == 8, "全量模式显示 6 个窗口 + 2 个表头");

        bool labelled = false;
        for (int i = 0; i < list->count(); ++i) {
            const QString text = rowText(list, i);
            if (text.contains(QStringLiteral("微信")) &&
                text.contains(QStringLiteral("学习"))) {
                labelled = true;
            }
        }
        check(labelled, "全量模式下每行标出所属工作区");

        showAll->setChecked(false);
        pump(app);
        check(windowRows(list) == 3, "退出全量模式后回到当前页");
    }

    // 4. 选中一个窗口后把它移走：选中被清掉，状态栏说明去了哪
    list->setCurrentRow(0);
    pump(app);
    const crossdesk::ui::WindowId movedId = currentRowId(list);
    check(movedId != 0, "可以选中一个窗口");
    service.moveWindowToWorkspace(movedId, QStringLiteral("base"));
    pump(app);
    check(windowRows(list) == 2, "移走一个窗口后本页少一个");
    // 移动是 service 命令发起的，它的 operationFinished 消息更具体（带窗口标题），
    // 会覆盖界面的"窗口已移到…"。这里只要求状态栏确实给了移动的反馈。
    check(status && status->text().contains(QStringLiteral("移动")),
          "状态栏给出移动结果");

    // 5. 窗口被外部关闭：选中被清掉，状态栏说明是关闭而不是移走
    list->setCurrentRow(0);
    pump(app);
    const crossdesk::ui::WindowId closedId = currentRowId(list);
    service.simulateWindowClosed(closedId);
    pump(app);
    check(windowRows(list) == 1, "关闭一个窗口后本页少一个");
    check(status && status->text().contains(QStringLiteral("关闭")),
          "状态栏说明窗口已关闭（不是移走）");

    // 6. 清空当前工作区：出现占位提示，移动按钮变灰
    const crossdesk::ui::OverviewSnapshot snapshot = service.snapshot();
    for (const crossdesk::ui::WindowItem& item : snapshot.windows) {
        if (item.workspaceId == snapshot.currentWorkspaceId) {
            service.simulateWindowClosed(item.id);
        }
    }
    pump(app);
    check(windowRows(list) == 0, "清空后没有可选窗口");
    check(list->count() == 1 &&
              list->item(0)->text().contains(QStringLiteral("还没有窗口")),
          "空工作区显示占位提示");
    QPushButton* move = findButton(window, QStringLiteral("移到其他工作区"));
    check(move && !move->isEnabled(), "没有选中时「移动」按钮不可用");

    // 7. 重置假数据：回到初始状态
    service.reset();
    pump(app);
    check(windowRows(list) == 3, "重置假数据后回到 3 个窗口");

    std::printf("\n%s：%d 项失败\n", failures == 0 ? "全部通过" : "存在失败", failures);
    return failures == 0 ? 0 : 1;
}
