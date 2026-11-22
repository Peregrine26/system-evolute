#pragma once

#include "window_item.h"

#include <QObject>
#include <QPixmap>
#include <QRect>
#include <QVector>

namespace crossdesk::ui {

// UI 与核心之间唯一的边界。
//
// 界面只依赖这个抽象：不碰 HWND / X11，也不自己维护第二份"窗口在哪"的真相。
// 所有命令都不返回 bool —— 真实的置前和设位置是系统调用，"返回 true"只能说明
// 命令发出去了，不能说明窗口真的动了。结果一律通过 operationFinished 逐窗报告，
// 这样"两个窗口里有一个失败"才说得清是哪一个。
class CrossDeskService : public QObject {
    Q_OBJECT

public:
    explicit CrossDeskService(QObject* parent = nullptr) : QObject(parent) {}
    ~CrossDeskService() override = default;

    virtual QVector<WindowItem> windows() const = 0;

    // 屏幕可用区域，与窗口位置同一坐标系。A 负责提供；接入前由 Qt 的屏幕 API 顶替。
    virtual QRect workArea() const = 0;

    virtual void refresh() = 0;
    virtual void activateWindow(WindowId window) = 0;
    virtual void placeWindow(WindowId window, const QRect& target) = 0;

    // 该窗口的预览画面。真实模式下由 A 的 DWM 缩略图注入到卡片控件上，
    // 这个方法返回空；假数据模式下由 FakeService 程序化绘制一张
    // "看起来像这个应用"的图，免得卡片只剩占位条。
    virtual QPixmap windowPreview(WindowId window) const { Q_UNUSED(window); return {}; }

signals:
    void windowsChanged();
    void operationFinished(crossdesk::ui::OperationResult result);
};

}  // namespace crossdesk::ui
