#pragma once

#include "window_item.h"

#include <QPixmap>
#include <QVector>
#include <QWidget>

namespace crossdesk::ui {

// 左栏的一张窗口卡：上面是这块窗口的画面，下面一行标题 + 进程名。
//
// 为什么不做成"一行文字 + 小缩略图"：这一栏要能一眼挑出"我要看哪个窗口"，
// 画面得占主要面积，文字只是标注；而且叠起来的时候，露出来的正好是下面那行字。
class WindowCard : public QWidget {
    Q_OBJECT

public:
    explicit WindowCard(QWidget* parent = nullptr);

    void setWindow(const WindowItem& window);
    void setSelected(bool on);
    bool isSelected() const { return m_selected; }

    // 叠影时上面那张会压住这张的顶部，画一道内阴影让层次看得出来。
    void setOverlapped(bool on);

    // 压缩模式：窗口多到连一条标题都排不下时，整张卡退化成一条标题，
    // 不再画画面区。宁可没有预览，也不能让两行字叠在一起。
    void setCompact(bool on);

    WindowId windowId() const { return m_id; }
    QString title() const { return m_title; }

    // 给 A 的 DWM 缩略图留的入口，和 ThumbnailSlot 一致。
    void setPreview(const QPixmap& pixmap);
    quintptr hostHandle();
    QRect screenRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    WindowId m_id = 0;
    QString m_title;
    QString m_process;
    QPixmap m_preview;
    bool m_selected = false;
    bool m_overlapped = false;
    bool m_compact = false;
};

// 左栏的卡片堆。
//
// 两种摆法，取决于窗口数量：
//   - 四个以内：竖着平铺，每张等分高度，正好铺满整条栏；
//   - 多于四个：叠起来，最前那张完整显示，其余每张只露出下面一条标题。
//     点哪张哪张就到最前；按住拖动可以把它从叠影里抽出来。
class CardStack : public QWidget {
    Q_OBJECT

public:
    explicit CardStack(QWidget* parent = nullptr);

    void setWindows(const QVector<WindowItem>& windows);
    void setSelected(WindowId id);
    WindowId selected() const { return m_selected; }

    int cardCount() const { return m_cards.size(); }
    bool isStacked() const { return m_stacked; }
    // 相邻两张的间距（叠影时就是每张露出来的那一条有多高）。
    int stackedStep() const { return m_step; }
    bool isCompact() const { return m_compact; }
    // 排在最前的那张（叠影时就是完整露出来的那张）。
    WindowId topWindow() const { return m_cards.isEmpty() ? 0 : m_cards.first()->windowId(); }

signals:
    void windowClicked(WindowId id);
    void windowActivated(WindowId id);  // 双击：把真实窗口切到前台

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void rebuildCards();
    void relayout(bool animated = false);
    void promote(int index);
    int cardIndexAt(const QPoint& pos) const;

    QVector<WindowCard*> m_cards;
    QVector<WindowItem> m_windows;  // 与 m_cards 同序
    WindowId m_selected = 0;
    bool m_stacked = false;
    bool m_compact = false;
    int m_step = 0;

    int m_pressIndex = -1;
    QPoint m_pressPos;
    QPoint m_dragOffset;
    bool m_dragging = false;
};

}  // namespace crossdesk::ui
