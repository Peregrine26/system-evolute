#pragma once

#include "window_item.h"

#include <QPixmap>
#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

class QVariantAnimation;

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
    QRect contentRect() const;
    QRect previewRect() const;  // Top-level client logical pixels for A.

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
// 四张以内平铺：四张一样大，从上到下把整条栏填满，四张都看得见。
// 第一张背后垫三层影子界面（往右下错开），代表"它下面还压着一摞"；
// 影子是画出来的，卡片拖动时跟着走，看起来像从那一摞里抽出来的。
// 鼠标压到右边那条外露的影子上时，第一张往上提一截——动手前的预告。
//
// 超过四张就平铺不下了，改成一列带标题的叠影：每张往下挪一段，露出下面那条标题，
// 点或拖都能换到最前。再多到连一条标题都排不下时，退化成一条条标题（`isCompact()`）。
class CardStack : public QWidget {
    Q_OBJECT

public:
    explicit CardStack(QWidget* parent = nullptr);

    void setWindows(const QVector<WindowItem>& windows);
    void setSelected(WindowId id);
    WindowId selected() const { return m_selected; }

    int cardCount() const { return m_cards.size(); }
    // 超过四张：改成一列带标题的叠影。
    bool isStacked() const { return m_stacked; }
    // 第一张卡背后垫了几层影子界面（0 = 没垫，只在平铺时才有）。
    int shadowLayers() const { return m_shadowLayers; }
    // 影子往右下错开的总量：卡片右边、下边各要为它留出这么宽。
    int shadowSpan() const;
    // 鼠标是否正压在外露的那条影子上（第一张已经提起来了）。
    bool isLifted() const { return m_hoverEdge; }
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
    void leaveEvent(QEvent* event) override;

private:
    void rebuildCards();
    void relayout(bool animated = false);
    void placeCard(int index, const QRect& target, bool animated);
    void promote(int index);
    int cardIndexAt(const QPoint& pos) const;

    // 外露的那条影子：鼠标压上去就把第一张提起来。
    bool onShadowEdge(const QPoint& pos) const;
    void updateHover(const QPoint& pos);
    void setLiftTarget(bool on);
    void applyLift();
    void clearLift();

    QVector<WindowCard*> m_cards;
    QVector<WindowItem> m_windows;  // 与 m_cards 同序
    WindowId m_selected = 0;
    bool m_stacked = false;
    bool m_compact = false;
    int m_step = 0;
    int m_shift = 0;
    int m_shadowLayers = 0;
    // 影子画在哪：第一张卡的"落位"，不是它当前的位置。
    // 拖动时卡片跑开、影子留在原地，才看得出是从那一摞里抽出来的。
    QRect m_shadowBase;
    // 每张卡的落位（relayout 时记下）：悬停上提时从落位往上抬，影子按落位画不动。
    QVector<QRect> m_restGeo;
    int m_hoveredCard = -1;  // 当前悬停的卡（-1 = 无）

    // 提起程度 0..1，配一段 140ms 的补间：0 = 落位，1 = 抬起 kLiftHeight。
    qreal m_lift = 0.0;
    QVariantAnimation* m_liftAnim = nullptr;
    bool m_hoverEdge = false;

    int m_pressIndex = -1;
    QPoint m_pressPos;
    QPoint m_dragOffset;
    bool m_dragging = false;
    WindowId m_lastClickId = 0;
    QPoint m_lastClickPos;
    QElapsedTimer m_lastClickTimer;
};

}  // namespace crossdesk::ui
