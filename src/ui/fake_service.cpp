#include "fake_service.h"

#include <algorithm>

namespace crossdesk::ui {

namespace {

// 四个假窗口，故意覆盖三种情况：
//   - 两个普通应用窗口（能成功定位、能成功排列）
//   - 一个系统应用窗口（能列出、定位会被系统拒绝 —— 对应 M2 的失败路径）
//   - 一个标题很长的窗口（验证列表和预览里的省略号，不会把布局撑破）
//
// 位置按"真实桌面"来摆：左右各占半屏、中间两个浮窗压在上面。
// 只有这样才能看出这个界面要表达的东西——一屏里谁大谁小、谁压住了谁。
QVector<WindowItem> makeWindows() {
    QVector<WindowItem> windows;

    WindowItem explorer;
    explorer.id = 2;
    explorer.title = QStringLiteral("项目资料");
    explorer.processName = QStringLiteral("explorer.exe");
    explorer.bounds = QRect(0, 0, 1180, 1040);
    explorer.hasBounds = true;
    windows.push_back(explorer);

    WindowItem browser;
    browser.id = 4;
    browser.title = QStringLiteral("CrossDesk 需求讨论 - 浏览器");
    browser.processName = QStringLiteral("chrome.exe");
    browser.bounds = QRect(1180, 0, 740, 1040);
    browser.hasBounds = true;
    windows.push_back(browser);

    WindowItem notepad;
    notepad.id = 1;
    notepad.title = QStringLiteral("未命名 - 记事本");
    notepad.processName = QStringLiteral("notepad.exe");
    notepad.bounds = QRect(300, 180, 900, 640);
    notepad.hasBounds = true;
    windows.push_back(notepad);

    WindowItem settings;
    settings.id = 3;
    settings.title = QStringLiteral("设置");
    settings.processName = QStringLiteral("SystemSettings.exe");
    settings.bounds = QRect(760, 300, 820, 560);
    settings.hasBounds = true;
    windows.push_back(settings);

    return windows;
}

}  // namespace

FakeService::FakeService(QObject* parent)
    : CrossDeskService(parent),
      m_windows(makeWindows()),
      // 1920×1080 减去 40 像素任务栏：和真实单显示器场景一致，
      // 但它是常量而不是查询结果，所以只在假数据模式下使用。
      m_workArea(0, 0, 1920, 1040) {}

QVector<WindowItem> FakeService::windows() const {
    return m_windows;
}

QRect FakeService::workArea() const {
    return m_workArea;
}

void FakeService::refresh() {
    // 假数据不会自己变化，但要发出信号，界面才能确认"刷新这条路径是通的"。
    emit windowsChanged();
}

void FakeService::activateWindow(WindowId window) {
    const auto it = std::find_if(m_windows.begin(), m_windows.end(),
                                 [window](const WindowItem& item) { return item.id == window; });

    if (it == m_windows.end()) {
        emit operationFinished({window, false, QStringLiteral("这个窗口已经关闭，无法定位。")});
        return;
    }

    // M2 的失败路径：系统设置这类窗口拒绝被第三方置前。
    if (it->processName == QStringLiteral("SystemSettings.exe")) {
        emit operationFinished(
            {window, false,
             QStringLiteral("Windows 拒绝把「%1」置于前台（系统窗口限制）。").arg(it->title)});
        return;
    }

    emit operationFinished({window, true, QStringLiteral("已切到「%1」。").arg(it->title)});
}

void FakeService::placeWindow(WindowId window, const QRect& target) {
    const auto it = std::find_if(m_windows.begin(), m_windows.end(),
                                 [window](const WindowItem& item) { return item.id == window; });

    if (it == m_windows.end()) {
        emit operationFinished({window, false, QStringLiteral("这个窗口已经关闭，无法移动。")});
        return;
    }

    it->bounds = target;
    it->hasBounds = true;
    emit operationFinished(
        {window, true,
         QStringLiteral("已把「%1」放到 (%2, %3)，%4×%5。")
             .arg(it->title)
             .arg(target.left())
             .arg(target.top())
             .arg(target.width())
             .arg(target.height())});
}

void FakeService::simulateClose(WindowId window) {
    const auto it = std::find_if(m_windows.begin(), m_windows.end(),
                                 [window](const WindowItem& item) { return item.id == window; });
    if (it == m_windows.end()) {
        return;
    }
    m_windows.erase(it);
}

void FakeService::reset() {
    m_windows = makeWindows();
}

}  // namespace crossdesk::ui
