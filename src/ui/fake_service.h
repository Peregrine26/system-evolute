#pragma once

#include "crossdesk_service.h"

#include <QObject>
#include <QVector>

namespace crossdesk::ui {

// 假数据实现：不依赖任何系统调用，用于界面自测与"没有 A 的接口时"的完整演示。
//
// 它存在的意义不是"糊弄"，而是让界面在 A/B 的真实实现到位之前就能被完整验证：
// 界面依赖的是 CrossDeskService 抽象，换实现不改界面代码。
class FakeService : public CrossDeskService {
    Q_OBJECT

public:
    // windowCount 用来演示两种摆法：4 个平铺，8 个叠起来。
    explicit FakeService(QObject* parent = nullptr, int windowCount = 4);

    QVector<WindowItem> windows() const override;
    QRect workArea() const override;

    void refresh() override;
    void activateWindow(WindowId window) override;
    void placeWindow(WindowId window, const QRect& target) override;

    // 测试用：模拟窗口被用户关掉，之后 refresh() 才能看出"旧条目消失"。
    void simulateClose(WindowId window);
    void reset();

private:
    QVector<WindowItem> m_windows;
    QRect m_workArea;
    int m_windowCount = 4;
};

}  // namespace crossdesk::ui
