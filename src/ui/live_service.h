#pragma once

// 真实窗口数据源：用 A 的扫描器读当前桌面的顶层窗口，用平台命令层执行置前/设位置。
//
// 查询走 A 的 WindowsWindowScanner（已有）；命令走 Win32WindowCommands
// （src/platform/windows/win32_window_commands.*，属于 A 的地盘，当前是兜底实现）。
// 命令一律通过 operationFinished 逐窗回报成功/失败，界面不假设命令一定成功。

#include "crossdesk_service.h"

#include <cstdint>
#include <memory>

namespace crossdesk {
class Win32ApiAdapter;
class WindowsWindowScanner;
class Win32WindowCommands;
}  // namespace crossdesk

namespace crossdesk::ui {

class LiveService : public CrossDeskService {
    Q_OBJECT

public:
    explicit LiveService(QObject* parent = nullptr);
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

    std::unique_ptr<crossdesk::Win32ApiAdapter> m_api;
    std::unique_ptr<crossdesk::WindowsWindowScanner> m_scanner;
    std::unique_ptr<crossdesk::Win32WindowCommands> m_commands;
};

}  // namespace crossdesk::ui
