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
    // coverEdge 是"上一张的下边缘在哪"（卡片内部坐标）：阴影从这条线往下压，
    // 而不是从卡片自己的顶边压——顶边本来就被盖住了，画在那儿谁也看不见。
    void setOverlapped(bool on);
    void setCoverEdge(int y);
    // 在叠影里的第几层（0 = 最前）。越深画得越暗，像一层层压出来的重影。
    void setDepth(int depth);

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
    int m_coverEdge = 0;
    int m_depth = 0;
};

// 左栏的卡片堆。
//
// 只有一张时整栏给它；两张起就斜着叠起来——
// 最前那张完整显示，其余每张往下挪一段、往右错一点，只露出下面那条标题，
// 像一摞卡片投出来的重影。点哪张哪张就到最前，按住拖动也可以把它抽出来。
// 窗口多到连一条标题都排不下时，退化成一条条标题（`isCompact()`）。
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
    // 每深一张往右错开多少像素（斜着叠的那个斜度）。
    int stackedShift() const { return m_shift; }
    bool isCompact() const { return m_compact; }
    WindowCard* cardAt(int index) const { return m_cards.value(index, nullptr); }
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
    int m_shift = 0;

    int m_pressIndex = -1;
    QPoint m_pressPos;
    QPoint m_dragOffset;
    bool m_dragging = false;
};

}  // namespace crossdesk::ui
