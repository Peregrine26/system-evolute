#include "live_service.h"

#include "platform/windows/win32_api_adapter.h"
#include "platform/windows/win32_window_commands.h"
#include "windows_window_scanner.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstdint>

namespace crossdesk::ui {

namespace {

// A 的枚举返回"全部顶层窗口"，实测 411 个，绝大多数是 1x1 的辅助窗口
// （GDI+ Window、IME、隐藏的消息窗口之类）。下面是**显示层过滤**，
// 不是过滤规则的最终答案——真正的规则应由 A 的扫描器/B 的筛选层定义。
constexpr int kMinWidth = 160;
constexpr int kMinHeight = 90;

QString fromWide(const std::wstring& text) {
    return QString::fromStdWString(text);
}

}  // namespace

LiveService::LiveService(QObject* parent)
    : LiveService(std::make_unique<crossdesk::Win32ApiAdapter>(), parent) {}

LiveService::LiveService(std::unique_ptr<crossdesk::WindowApiAdapter> api, QObject* parent)
    : CrossDeskService(parent), m_api(std::move(api)) {
    if (!m_api) m_api = std::make_unique<crossdesk::Win32ApiAdapter>();
    m_scanner = std::make_unique<crossdesk::WindowsWindowScanner>(*m_api);
    m_selfPid = static_cast<std::uint32_t>(QCoreApplication::applicationPid());

    // 屏幕可用区域先用 Qt 的查询顶替，等 A 提供与窗口位置同坐标系的接口后替换。
    //
    // 乘 devicePixelRatio 的原因：Win32 在 DPI 感知进程里返回物理像素，
    // 而 Qt 的 availableGeometry 是逻辑像素。125% 缩放下两者相差 1.25 倍，
    // 不换算的话算出来的排列矩形会整体偏小。
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect logical = screen->availableGeometry();
        const qreal ratio = screen->devicePixelRatio();
        m_workArea = QRect(qRound(logical.x() * ratio),
                           qRound(logical.y() * ratio),
                           qRound(logical.width() * ratio),
                           qRound(logical.height() * ratio));
    }
}

LiveService::~LiveService() = default;

QVector<WindowItem> LiveService::windows() const {
    return m_windows;
}

QRect LiveService::workArea() const {
    return m_workArea;
}

void LiveService::refresh() {
    const auto scan = m_scanner->scan_with_status();
    if (!scan.complete) {
        emit operationFinished({0, false, QStringLiteral("窗口扫描失败，保留上次列表。")});
        return;
    }
    const auto& scanned = scan.windows;

    QVector<WindowItem> windows;
    windows.reserve(static_cast<int>(scanned.size()));

    for (const crossdesk::WindowInfo& info : scanned) {
        if (!info.visible || info.title.empty()) {
            continue;
        }
        if (info.process_id == m_selfPid) {
            continue;  // 别把总览自己列进去
        }
        // 桌面本身和输入法候选窗不是"应用窗口"：它们各占一个位置，
        // 桌面那层还会在画面上铺满整块、把别的窗口全盖住。
        if (info.process_name == L"explorer.exe" && info.title == L"Program Manager") {
            continue;
        }
        if (info.process_name == L"TextInputHost.exe") {
            continue;
        }
        const int width = info.bounds.right - info.bounds.left;
        const int height = info.bounds.bottom - info.bounds.top;
        if (width < kMinWidth || height < kMinHeight) {
            continue;
        }

        WindowItem item;
        item.id = static_cast<WindowId>(info.native_handle);
        item.title = QString::fromStdWString(info.title);
        item.processName = QString::fromStdWString(info.process_name);
        item.bounds = QRect(info.bounds.left, info.bounds.top, width, height);
        item.hasBounds = true;
        windows.push_back(item);
    }

    // Sort for display, retaining every valid candidate regardless of count.
    std::stable_sort(windows.begin(), windows.end(),
                     [](const WindowItem& a, const WindowItem& b) {
                         const qint64 areaA = qint64(a.bounds.width()) * a.bounds.height();
                         const qint64 areaB = qint64(b.bounds.width()) * b.bounds.height();
                         return areaA > areaB;
                     });

    m_windows = windows;
    emit windowsChanged();
}

void LiveService::activateWindow(WindowId window) {
    runCommand(window, {}, true);
}

void LiveService::placeWindow(WindowId window, const QRect& target) {
    runCommand(window, target, false);
}

void LiveService::runCommand(WindowId window, const QRect& target, bool activate) {
    if (m_commandPending) {
        emit operationFinished({window, false, QStringLiteral("上一项窗口操作仍在核对，请稍后重试。")});
        return;
    }
    m_commandPending = true;
    auto* watcher = new QFutureWatcher<OperationResult>(this);
    connect(watcher, &QFutureWatcher<OperationResult>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        m_commandPending = false;
        watcher->deleteLater();
        emit operationFinished(result);
    });
    // Capture values only. A deleted service neither waits for a foreign window
    // nor receives the worker's completion; its child watcher disconnects it.
    watcher->setFuture(QtConcurrent::run([window, target, activate] {
        crossdesk::Win32WindowCommands commands;
        std::wstring error;
        const bool ok = activate
            ? commands.activate(static_cast<std::uintptr_t>(window), &error)
            : commands.place(static_cast<std::uintptr_t>(window), target.x(), target.y(),
                             target.width(), target.height(), &error);
        return OperationResult{window, ok, ok
            ? (activate ? QStringLiteral("已切换到目标窗口。") : QStringLiteral("已排列。"))
            : QStringLiteral("%1失败：%2").arg(activate ? QStringLiteral("定位")
                                                        : QStringLiteral("排列"), fromWide(error))};
    }));
}

}  // namespace crossdesk::ui
