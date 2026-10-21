#pragma once

#include "window_item.h"

#include <QPixmap>
#include <QVector>
#include <QWidget>

namespace crossdesk::ui {

// 一个窗口在"桌面画面"里的替身。
//
// 现在画的是"标题栏 + 内容占位"——这不是偷懒，而是 M6 明确要求的降级：
// 拿不到画面时也要能认出是哪个窗口，且不影响其它窗口的显示。
//
// A 的 DWM 缩略图接入后，由 A 用 hostHandle() 拿到宿主窗口句柄、用 screenRect()
// 拿到要填充的屏幕矩形去注册缩略图，然后调 setPreview() 把内容塞进来。
// UI 不自己截屏，也不 include windows.h。
class ThumbnailSlot : public QWidget {
    Q_OBJECT

public:
    explicit ThumbnailSlot(QWidget* parent = nullptr);

    void setWindow(const WindowItem* window);  // 传 nullptr 表示这个位置空着
    void setBadge(const QString& text);        // "左" / "右"，排列模式下才有
    void setHighlighted(bool on);              // 被勾选的窗口描蓝边
    bool isHighlighted() const { return m_highlighted; }

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
    bool m_highlighted = false;
};

// 桌面画面：整块区域就是一整块"主屏幕"的缩小版。
//
// 两种用法，画的是同一件事——"这些窗口在屏幕上的哪里"：
//   showDesktop()     ：按每个窗口的真实位置摆，窗口之间自然会重叠，
//                       看起来就是当前桌面的样子；
//   showArrangement()：只摆要排列的那两个，位置来自 B 的布局结果，
//                       也就是"排完之后桌面会变成什么样"。
//
// 这里只做"缩放 + 摆放"，不参与任何布局计算——比例不对就是 B 的输入不对，
// 界面不做二次修正，否则出了问题分不清是谁的锅。
class LayoutPreview : public QWidget {
    Q_OBJECT

public:
    explicit LayoutPreview(QWidget* parent = nullptr);

    void showDesktop(const QRect& workArea,
                     const QVector<WindowItem>& windows,
                     const QVector<WindowId>& highlighted);
    void showArrangement(const QRect& workArea,
                         const QVector<Placement>& placements,
                         const QVector<WindowItem>& windows);

    // 只显示一个窗口，铺满整块画面：右栏就是"这个窗口现在长什么样"。
    void showSingle(const QRect& workArea, const WindowItem& window);

    void clear();

    int paneCount() const;  // 当前真正显示出来的窗口块数

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void updateFrame();
    void applyLayout();
    void hideAllPanes();

    QRect m_workArea;  // 服务给的可用区域（屏幕坐标）
    QRect m_area;      // 实际参与比例换算的区域（拿不到真实值时用兜底比例）
    QRect m_frame;     // 屏幕框在本控件里的矩形
    qreal m_scaleX = 1.0;
    qreal m_scaleY = 1.0;

    QVector<WindowItem> m_items;  // 当前要画的窗口，顺序 = 叠放顺序（先画的在下面）
    QVector<QRect> m_rects;       // 与 m_items 一一对应的目标矩形
    QVector<WindowId> m_highlighted;
    QVector<QString> m_badges;
    QVector<ThumbnailSlot*> m_panes;  // 复用的槽位池，只增不减
};

}  // namespace crossdesk::ui
