#pragma once

#include "window_item.h"

#include <QPixmap>
#include <QVector>
#include <QWidget>

namespace crossdesk::ui {

// 一路窗口预览。
//
// 现在显示的是"标题 + 进程名"的占位——这不是偷懒，而是 M6 明确要求的降级：
// 单个来源不可预览时也要能认出窗口，且不影响其它预览和列表。
//
// A 的 DWM 缩略图接入后，由 A 用 hostHandle() 拿到宿主窗口句柄、用 screenRect()
// 拿到要填充的屏幕矩形去注册缩略图，然后调 setPreview() 把内容塞进来。
// UI 不自己截屏，也不 include windows.h。
class ThumbnailSlot : public QWidget {
    Q_OBJECT

public:
    explicit ThumbnailSlot(QWidget* parent = nullptr);

    void setWindow(const WindowItem* window);  // 传 nullptr 表示这个位置空着
    void setBadge(const QString& text);        // 预览页里的"左/右"角标

    void setPreview(const QPixmap& pixmap);
    void clearPreview();

    WindowId windowId() const { return m_id; }

    // 给 A 注册 DWM 缩略图用。第一次调用会创建原生窗口，按需调用。
    quintptr hostHandle();
    QRect screenRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    WindowId m_id = 0;
    QString m_title;
    QString m_process;
    QString m_badge;
    QPixmap m_preview;
};

// 左右排列的预览画布。
//
// 它把 B 给出的目标矩形按比例画出来，让用户在看之前就知道两个窗口会落在哪。
// 这里只做"缩放 + 摆放"，不参与任何布局计算——比例不对就是 B 的输入不对，
// 界面不做二次修正，否则出了问题分不清是谁的锅。
class LayoutPreview : public QWidget {
    Q_OBJECT

public:
    explicit LayoutPreview(QWidget* parent = nullptr);

    void setPlacements(const QRect& workArea,
                       const QVector<Placement>& placements,
                       const QVector<WindowItem>& windows);
    void clear();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void relayout();

    QRect m_workArea;
    QRect m_frame;
    QVector<Placement> m_placements;
    QVector<WindowItem> m_windows;
    ThumbnailSlot* m_left = nullptr;
    ThumbnailSlot* m_right = nullptr;
};

}  // namespace crossdesk::ui
