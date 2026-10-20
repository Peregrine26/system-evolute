#include "live_service.h"

#include "platform/windows/win32_api_adapter.h"
#include "windows_window_scanner.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>

#include <cstdint>

namespace crossdesk::ui {

namespace {

// A 的枚举返回"全部顶层窗口"，实测 411 个，绝大多数是 1x1 的辅助窗口
// （GDI+ Window、IME、隐藏的消息窗口之类）。下面是**显示层过滤**，
// 不是过滤规则的最终答案——真正的规则应由 A 的扫描器/B 的筛选层定义。
constexpr int kMinWidth = 160;
constexpr int kMinHeight = 90;

QString notWired(const QString& what) {
    return QStringLiteral("%1需要平台命令接口，当前只接了扫描（等待 A 的接口）。").arg(what);
}

}  // namespace

LiveService::LiveService(QObject* parent) : CrossDeskService(parent) {
    m_api = std::make_unique<crossdesk::Win32ApiAdapter>();
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
    const std::vector<crossdesk::WindowInfo> scanned = m_scanner->scan();

    QVector<WindowItem> windows;
    windows.reserve(static_cast<int>(scanned.size()));

    for (const crossdesk::WindowInfo& info : scanned) {
        if (!info.visible || info.title.empty()) {
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
        item.id = static_cast<WindowId>(info.native_handle);
        item.title = QString::fromStdWString(info.title);
        item.processName = QString::fromStdWString(info.process_name);
        item.bounds = QRect(info.bounds.left, info.bounds.top, width, height);
        item.hasBounds = true;
        windows.push_back(item);
    }

    m_windows = windows;
    emit windowsChanged();
}

void LiveService::activateWindow(WindowId window) {
    // 这条最容易被误解：界面上点一下"就回到窗口"看起来像 UI 的功劳，
    // 但真正的置前是 A 的平台命令，界面无权直接调 Win32。
    emit operationFinished({window, false, notWired(QStringLiteral("定位窗口"))});
}

void LiveService::placeWindow(WindowId window, const QRect&) {
    emit operationFinished({window, false, notWired(QStringLiteral("设置窗口位置"))});
}

}  // namespace crossdesk::ui
