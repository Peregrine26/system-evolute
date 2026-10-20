#pragma once

// 真实窗口数据源：用 A 的扫描器读当前桌面的顶层窗口。
//
// 它只做 M1（扫描 + 刷新）和"屏幕可用区域"这两件只读的事；
// 置前、设位置属于 A 的命令接口，当前仓库里还没有，因此一律返回失败并说明原因。
// 这里不假装成功——"点了没反应"比"报错说没接入"更难排查。

#include "crossdesk_service.h"

#include <cstdint>
#include <memory>

namespace crossdesk {
class Win32ApiAdapter;
class WindowsWindowScanner;
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
};

}  // namespace crossdesk::ui
