#include "live_workspace_service.h"

#include "platform/windows/win32_api_adapter.h"
#include "windows_window_scanner.h"

#include <QCoreApplication>
#include <QStringList>

namespace crossdesk::ui {

namespace {

// 单工作区：归属规则是 B 的事，这里不猜。
const QString kLiveWorkspaceId = QStringLiteral("live");

// A 的枚举是"全部顶层窗口"，实测 411 个，绝大多数是 1x1 的辅助窗口
// （GDI+ Window、IME、隐藏的消息窗口之类）。下面是**显示层过滤**，
// 不是枚举规则的最终答案——真正的过滤条件应由 A 的扫描器/B 的状态层定义。
constexpr int kMinWidth = 160;
constexpr int kMinHeight = 90;

QString describeFailure(const QString& what) {
    return QStringLiteral("只读模式：%1需要核心模块的命令接口，当前未接入。").arg(what);
}

}  // namespace

LiveWorkspaceService::LiveWorkspaceService(QObject* parent) : WorkspaceService(parent) {
    m_api = std::make_unique<crossdesk::Win32ApiAdapter>();
    m_scanner = std::make_unique<crossdesk::WindowsWindowScanner>(*m_api);
    m_selfPid = static_cast<std::uint32_t>(QCoreApplication::applicationPid());

    // 800ms 是给真实用户看的刷新节奏：再快没有意义（窗口不会 100ms 变一次），
    // 再慢就会出现"关掉窗口后总览里还留着"的错觉。
    m_timer.setInterval(800);
    connect(&m_timer, &QTimer::timeout, this, &LiveWorkspaceService::refresh);
    m_timer.start();

    refresh();
}

LiveWorkspaceService::~LiveWorkspaceService() = default;

OverviewSnapshot LiveWorkspaceService::snapshot() const {
    return m_snapshot;
}

void LiveWorkspaceService::refreshNow() {
    refresh();
}

void LiveWorkspaceService::refresh() {
    const std::vector<crossdesk::WindowInfo> scanned = m_scanner->scan();

    QVector<WindowItem> windows;
    windows.reserve(static_cast<int>(scanned.size()));

    for (const crossdesk::WindowInfo& info : scanned) {
        if (!info.visible) {
            continue;
        }
        if (info.title.empty()) {
            continue;
        }
        if (info.process_id == m_selfPid) {
            continue;  // 别把总览自己列进去
        }
        const int width = info.bounds.right - info.bounds.left;
        const int height = info.bounds.bottom - info.bounds.top;
        if (width < kMinWidth || height < kMinHeight) {
            continue;
        }

        WindowItem item;
        item.id = info.native_handle;
        item.title = QString::fromStdWString(info.title);
        item.appName = QString::fromStdWString(info.process_name);
        item.workspaceId = kLiveWorkspaceId;
        item.control = ControlState::Manageable;
        item.visibility = Visibility::Visible;
        windows.push_back(item);
    }

    // 指纹用"句柄 + 标题"：标题变了（比如浏览器换标签）也要刷新。
    QStringList parts;
    parts.reserve(windows.size());
    for (const WindowItem& window : windows) {
        parts << QStringLiteral("%1|%2").arg(window.id).arg(window.title);
    }
    const QString signature = parts.join(QLatin1Char('\n'));

    if (signature == m_signature && !m_snapshot.workspaces.isEmpty()) {
        return;  // 没变化，不打扰界面
    }
    m_signature = signature;

    WorkspaceItem workspace;
    workspace.id = kLiveWorkspaceId;
    workspace.name = QStringLiteral("当前桌面");
    workspace.windowCount = windows.size();

    m_snapshot.workspaces = {workspace};
    m_snapshot.currentWorkspaceId = kLiveWorkspaceId;
    m_snapshot.windows = windows;

    emit snapshotChanged(m_snapshot);
}

void LiveWorkspaceService::switchWorkspace(const WorkspaceId& workspaceId) {
    if (workspaceId == m_snapshot.currentWorkspaceId) {
        return;  // 只有一个工作区，切自己等于什么也没做
    }
    emit operationFinished(0, false, describeFailure(QStringLiteral("切换工作区")));
}

void LiveWorkspaceService::moveWindowToWorkspace(WindowId window, const WorkspaceId&) {
    emit operationFinished(window, false, describeFailure(QStringLiteral("移动归属")));
}

void LiveWorkspaceService::activateWindow(WindowId window) {
    // 这条最容易被误解：界面点一下就"回到窗口"看起来像 UI 的功劳，
    // 但真正的置前是 A 的平台命令，界面无权直接调 Win32。
    emit operationFinished(window, false, describeFailure(QStringLiteral("激活窗口")));
}

void LiveWorkspaceService::retryFailedOperations() {
    refreshNow();
}

}  // namespace crossdesk::ui
