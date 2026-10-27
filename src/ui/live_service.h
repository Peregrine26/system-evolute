#pragma once

// 真实窗口数据源：用 A 的扫描器读当前桌面的顶层窗口，用平台命令层执行置前/设位置。
//
// 查询走 A 的 WindowsWindowScanner；命令在工作线程发出并限时观察真实结果。
// operationFinished 异步回报；未确认或被拒绝都不声称成功。

#include "crossdesk_service.h"

#include <cstdint>
#include <memory>

namespace crossdesk {
class WindowApiAdapter;
class WindowsWindowScanner;
}  // namespace crossdesk

namespace crossdesk::ui {

class LiveService : public CrossDeskService {
    Q_OBJECT

public:
    explicit LiveService(QObject* parent = nullptr);
    explicit LiveService(std::unique_ptr<crossdesk::WindowApiAdapter> api, QObject* parent = nullptr);
    ~LiveService() override;

    QVector<WindowItem> windows() const override;
    QRect workArea() const override;

    void refresh() override;
    void activateWindow(WindowId window) override;
    void placeWindow(WindowId window, const QRect& target) override;

private:
    QVector<WindowItem> m_windows;
    QRect m_workArea;
    std::uint32_t m_selfPid = 0;

    std::unique_ptr<crossdesk::WindowApiAdapter> m_api;
    std::unique_ptr<crossdesk::WindowsWindowScanner> m_scanner;
    // Only one outstanding command; worker results return through a watcher
    // owned by this service, so destruction never leaves a callback to it.
    bool m_commandPending = false;
    void runCommand(WindowId window, const QRect& target, bool activate);
};

}  // namespace crossdesk::ui
