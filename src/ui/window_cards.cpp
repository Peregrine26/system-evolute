#include "window_cards.h"

#include <QEasingCurve>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>

namespace crossdesk::ui {

namespace {

constexpr int kMargin = 12;
constexpr int kGap = 10;
constexpr int kTextHeight = 46;
constexpr int kCardRadius = 10;

const QColor kAccent(0x0a, 0x84, 0xff);

}  // namespace

WindowCard::WindowCard(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);  // 点击统一交给 CardStack 判断
}

void WindowCard::setWindow(const WindowItem& window) {
    if (m_id == window.id && m_title == window.title) {
        return;
    }
    m_id = window.id;
    m_title = window.title;
    m_process = window.processName;
    m_preview = QPixmap();
    update();
}

void WindowCard::setSelected(bool on) {
    if (m_selected == on) {
        return;
    }
    m_selected = on;
    update();
}

void WindowCard::setOverlapped(bool on) {
    if (m_overlapped == on) {
        return;
    }
    m_overlapped = on;
    update();
}

void WindowCard::setCoverEdge(int y) {
    if (m_coverEdge == y) {
        return;
    }
    m_coverEdge = y;
    update();
}

void WindowCard::setDepth(int depth) {
    if (m_depth == depth) {
        return;
    }
    m_depth = depth;
    update();
}

void WindowCard::setCompact(bool on) {
    if (m_compact == on) {
        return;
    }
    m_compact = on;
    update();
}

void WindowCard::setPreview(const QPixmap& pixmap) {
    m_preview = pixmap;
    update();
}

quintptr WindowCard::hostHandle() {
    return static_cast<quintptr>(winId());
}

QRect WindowCard::screenRect() const {
    return QRect(mapToGlobal(QPoint(0, 0)), size());
}

void WindowCard::paintEvent(QPaintEvent*) {
    if (m_id == 0) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath cardPath;
    cardPath.addRoundedRect(card, kCardRadius, kCardRadius);

    // ---- 压缩模式：整张卡就是一条标题 ----
    // 窗口多到叠影也放不下完整的一条标题时走这里：不画画面区，也不画进程名，
    // 只留一行标题。再多的窗口也不会出现两行字叠在一起的情况。
    if (m_compact) {
        // 一条只有二三十像素高，用大圆角会变成一排药丸，所以这里单独收小。
        QPainterPath stripPath;
        stripPath.addRoundedRect(card, 4, 4);
        painter.fillPath(stripPath, m_selected ? QColor(0x2c, 0x34, 0x43) : QColor(0x20, 0x24, 0x2c));

        const QString title = m_title.isEmpty() ? QStringLiteral("（无标题窗口）") : m_title;
        QFont titleFont = font();
        titleFont.setPixelSize(13);
        titleFont.setBold(m_selected);
        painter.setFont(titleFont);
        painter.setPen(m_selected ? QColor(0xf2, 0xf5, 0xf9) : QColor(0xd6, 0xda, 0xe1));
        const int dotRoom = m_selected ? 22 : 12;
        painter.drawText(QRectF(12, 0, width() - 12 - dotRoom, height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(titleFont).elidedText(title, Qt::ElideRight,
                                                           width() - 12 - dotRoom));

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(m_selected ? kAccent : QColor(255, 255, 255, 20), m_selected ? 1.6 : 1.0));
        painter.drawPath(stripPath);

        if (m_selected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(kAccent);
            painter.drawEllipse(QPointF(width() - 14, height() / 2.0), 3.0, 3.0);
        }
        return;
    }

    painter.fillPath(cardPath, m_selected ? QColor(0x24, 0x2a, 0x35) : QColor(0x1b, 0x1e, 0x25));

    // ---- 画面区：一块小窗口 ----
    const int textTop = height() - kTextHeight;
    const QRectF screen = QRectF(12, 12, width() - 24, qMax(20, textTop - 12 - 8));
    QPainterPath screenPath;
    screenPath.addRoundedRect(screen, 6, 6);
    painter.fillPath(screenPath, QColor(0x15, 0x18, 0x1e));

    if (!m_preview.isNull()) {
        painter.save();
        painter.setClipPath(screenPath);
        const QPixmap scaled =
            m_preview.scaled(screen.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawPixmap(screen.center().toPoint() - QPoint(scaled.width() / 2, scaled.height() / 2),
                           scaled);
        painter.restore();
    } else if (screen.height() > 40) {
        // 占位：窗口标题栏 + 左侧窄栏 + 几行短横线。
        painter.save();
        painter.setClipPath(screenPath);
        const qreal barHeight = qBound(0.0, screen.height() * 0.16, 20.0);
        painter.fillRect(QRectF(screen.left(), screen.top(), screen.width(), barHeight),
                         QColor(0x2b, 0x30, 0x3a));
        painter.setPen(Qt::NoPen);

        const qreal bodyTop = screen.top() + barHeight;
        if (screen.height() - barHeight > 30 && screen.width() > 90) {
            const qreal sidebar = screen.width() * 0.14;
            painter.setBrush(QColor(255, 255, 255, 8));
            painter.drawRect(QRectF(screen.left(), bodyTop, sidebar, screen.height() - barHeight));

            const qreal lineLeft = screen.left() + sidebar + 10;
            const qreal maxWidth = screen.right() - lineLeft - 12;
            for (int i = 0; i < 3; ++i) {
                const qreal y = bodyTop + 12 + i * qMax(9.0, (screen.height() - barHeight) * 0.2);
                if (y + 4 > screen.bottom()) {
                    break;
                }
                painter.setBrush(QColor(255, 255, 255, 14));
                painter.drawRoundedRect(
                    QRectF(lineLeft, y, maxWidth * (i == 2 ? 0.55 : 0.9), 4), 2, 2);
            }
        }
        painter.restore();
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 16), 1));
    painter.drawPath(screenPath);

    // ---- 下面那行字：给一块略亮的底，叠起来时露出的就是这条"标题条" ----
    painter.save();
    painter.setClipPath(cardPath);
    painter.fillRect(QRectF(0, textTop, width(), height() - textTop),
                     m_selected ? QColor(0x2c, 0x34, 0x43) : QColor(0x20, 0x24, 0x2c));
    painter.restore();

    const QString title = m_title.isEmpty() ? QStringLiteral("（无标题窗口）") : m_title;
    QFont titleFont = font();
    titleFont.setPixelSize(13);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(m_selected ? QColor(0xf2, 0xf5, 0xf9) : QColor(0xd6, 0xda, 0xe1));
    painter.drawText(QRect(12, textTop, width() - 24, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, width() - 24));

    QFont subFont = font();
    subFont.setPixelSize(11);
    painter.setFont(subFont);
    painter.setPen(QColor(0x7d, 0x83, 0x8d));
    painter.drawText(QRect(12, textTop + 19, width() - 24, 16), Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetrics(subFont).elidedText(m_process, Qt::ElideRight, width() - 24));

    // ---- 选中：蓝边 + 右侧小圆点 ----
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(m_selected ? kAccent : QColor(255, 255, 255, 20), m_selected ? 2.0 : 1.0));
    painter.drawPath(cardPath);

    if (m_selected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(kAccent);
        painter.drawEllipse(QPointF(width() - 16, textTop + 9), 3.5, 3.5);
    }

    // 越深压得越暗：一摞叠起来时，后面的本来就该退到阴影里。
    if (m_depth > 0) {
        painter.save();
        painter.setClipPath(cardPath);
        painter.fillPath(cardPath, QColor(0, 0, 0, qMin(72, m_depth * 12)));
        painter.restore();
    }

    // 叠影时画一道从顶部压下来的阴影，卡片之间才有"前后"的感觉。
    if (m_overlapped) {
        // 从"上一张的下边缘"往下压一道阴影：叠影的层次全靠它，否则两段同色很难看出前后。
        painter.save();
        painter.setClipPath(cardPath);
        const qreal top = qBound(0, m_coverEdge, qMax(0, height() - 4));
        QLinearGradient shadow(0, top, 0, top + 22);
        shadow.setColorAt(0.0, QColor(0, 0, 0, 150));
        shadow.setColorAt(0.55, QColor(0, 0, 0, 60));
        shadow.setColorAt(1.0, QColor(0, 0, 0, 0));
        painter.fillRect(QRectF(0, top, width(), 22), shadow);
        painter.restore();
    }
}

CardStack::CardStack(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void CardStack::setWindows(const QVector<WindowItem>& windows) {
    m_windows = windows;
    rebuildCards();
    relayout();
}

void CardStack::rebuildCards() {
    // 卡片数量按需增删：窗口变少时必须真的删掉多余的卡片，
    // 否则 m_cards 和 m_windows 的下标会错开，点第一张选中的是第三张。
    while (m_cards.size() > m_windows.size()) {
        delete m_cards.takeLast();
    }
    while (m_cards.size() < m_windows.size()) {
        m_cards.push_back(new WindowCard(this));
    }

    for (int i = 0; i < m_cards.size(); ++i) {
        WindowCard* card = m_cards.at(i);
        card->setWindow(m_windows.at(i));
        card->setSelected(m_windows.at(i).id == m_selected);
        card->show();
    }
}

void CardStack::setSelected(WindowId id) {
    m_selected = id;
    for (WindowCard* card : m_cards) {
        card->setSelected(card->windowId() == id);
    }
}

void CardStack::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayout();
}

void CardStack::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    // 左栏自己一层底色，和右边的桌面画面分开，像任务栏挂在一块面板上。
    painter.fillRect(rect(), QColor(0x14, 0x17, 0x1c));
    painter.setPen(QColor(0x24, 0x28, 0x30));
    painter.drawLine(width() - 1, 0, width() - 1, height());
}

void CardStack::mouseDoubleClickEvent(QMouseEvent* event) {
    const int index = cardIndexAt(event->pos());
    if (index >= 0) {
        emit windowActivated(m_cards.at(index)->windowId());
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CardStack::relayout(bool animated) {
    const int count = m_windows.size();
    if (count == 0) {
        return;
    }

    const int width = this->width() - kMargin * 2;
    // 上下也留出同样的边：贴着窗口边缘会被切掉圆角和边框，看起来像溢出而不是铺满。
    const int inner = this->height() - kMargin * 2;

    // 一张的时候没得叠，整栏给它；两张起就斜着叠——参考图里本来就是叠着的。
    m_stacked = count > 1;

    int cardHeight = inner;
    int step = inner;
    int shift = 0;

    if (m_stacked) {
        // 最前面那张不必高过半栏；往下的这段至少要放得下一条标题条，
        // 否则下面那张的标题会被上面这张压掉一半。
        const int maxCard = qMin(280, inner / 2);
        step = qMax(kTextHeight, qMin(inner * 22 / 100, (inner - maxCard) / (count - 1)));
        cardHeight = inner - (count - 1) * step;

        // 连一条标题都排不下（大约 20 张以上）：不再叠影，退化成一条条标题。
        // 这时卡片高度就等于行高，相邻两张正好不重叠，字也就不会互相压。
        m_compact = cardHeight < kTextHeight;
        if (m_compact) {
            step = qMax(1, inner / count);
            cardHeight = step;
        }

        // 每深一张往右错一点，像一摞卡片投出来的重影；右边缘对齐，左边一级级往里收。
        // 横向总量压在栏宽的 40% 以内，否则最后几张会瘦成一条；压缩档不斜。
        shift = m_compact ? 0 : qBound(4, width * 40 / 100 / (count - 1), 20);
    } else {
        m_compact = false;
    }

    m_step = step;
    m_shift = shift;

    for (int i = 0; i < count; ++i) {
        const QRect target(kMargin + i * shift, kMargin + i * step, width - i * shift, cardHeight);
        WindowCard* card = m_cards.at(i);
        card->setCompact(m_compact);
        card->setCoverEdge(cardHeight - step);
        card->setDepth(m_compact ? 0 : i);
        if (animated && card->geometry() != target) {
            auto* anim = new QPropertyAnimation(card, "geometry", card);
            anim->setDuration(160);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            anim->setStartValue(card->geometry());
            anim->setEndValue(target);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        } else {
            card->setGeometry(target);
        }
    }
    // i=0 最后 raise，所以它压在最上面：视觉上"第一张在最前面"。
    for (int i = count - 1; i >= 0; --i) {
        m_cards.at(i)->raise();
        m_cards.at(i)->setOverlapped(!m_compact && i > 0);
    }
}

int CardStack::cardIndexAt(const QPoint& pos) const {
    if (m_stacked && !m_compact) {
        // 斜着叠之后每张真正露出来的只有下面这一条（最前那张整块都是它自己的），
        // 所以先按"露出来的那一条"判定，不能拿整张的矩形去撞。
        for (int i = 0; i < m_cards.size(); ++i) {
            WindowCard* card = m_cards.at(i);
            if (!card->isVisible()) {
                continue;
            }
            if (i == 0) {
                if (card->geometry().contains(pos)) {
                    return 0;
                }
                continue;
            }
            const QRect band(card->x(), card->y() + card->height() - m_step, card->width(),
                             m_step);
            if (band.contains(pos)) {
                return i;
            }
        }
        // 斜着叠会在左下角留出一条三角形空档。落在空档里时按行去找那一行是谁，
        // 否则那一片点了没反应，像坏了一样。
        for (int i = 1; i < m_cards.size(); ++i) {
            WindowCard* card = m_cards.at(i);
            if (!card->isVisible()) {
                continue;
            }
            if (pos.y() >= card->y() + card->height() - m_step && pos.y() < card->y() + card->height()) {
                return i;
            }
        }
        return -1;
    }

    for (int i = 0; i < m_cards.size(); ++i) {
        if (m_cards.at(i)->isVisible() && m_cards.at(i)->geometry().contains(pos)) {
            return i;
        }
    }
    return -1;
}

void CardStack::promote(int index) {
    if (index <= 0 || index >= m_cards.size()) {
        return;
    }
    m_cards.move(index, 0);
    if (index < m_windows.size()) {
        m_windows.move(index, 0);
    }
    relayout(true);
}

void CardStack::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const int index = cardIndexAt(event->pos());
    if (index < 0) {
        return;
    }
    m_pressIndex = index;
    m_pressPos = event->pos();
    m_dragOffset = m_cards.at(index)->pos() - event->pos();
    m_dragging = false;
}

void CardStack::mouseMoveEvent(QMouseEvent* event) {
    if (m_pressIndex < 0 || m_pressIndex >= m_cards.size()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!m_dragging && (event->pos() - m_pressPos).manhattanLength() > 6) {
        m_dragging = true;
        // 拖动时先把它提到最上面，跟着鼠标走，才看得出"被抽出来了"。
        m_cards.at(m_pressIndex)->raise();
        setCursor(Qt::ClosedHandCursor);
    }

    if (m_dragging) {
        WindowCard* card = m_cards.at(m_pressIndex);
        QPoint target = event->pos() + m_dragOffset;
        target.setY(qBound(0, target.y(), height() - card->height()));
        card->move(target);
    }
}

void CardStack::mouseReleaseEvent(QMouseEvent* event) {
    unsetCursor();

    if (m_pressIndex < 0 || m_pressIndex >= m_cards.size()) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const int index = m_pressIndex;
    const bool dragged = m_dragging;
    m_pressIndex = -1;
    m_dragging = false;

    WindowCard* card = m_cards.at(index);
    const WindowId id = card->windowId();

    if (m_stacked && index > 0) {
        promote(index);  // 点一下或拖出来，都把它放到最前面
    } else {
        relayout(true);  // 消除拖动留下的位置
    }

    emit windowClicked(id);
}

}  // namespace crossdesk::ui
