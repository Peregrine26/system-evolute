#include "window_cards.h"

#include <QEasingCurve>
#include <QApplication>
#include <QStyleHints>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QVariantAnimation>

namespace crossdesk::ui {

namespace {

constexpr int kMargin = 12;
constexpr int kTextHeight = 46;
constexpr int kCardRadius = 10;

// 平铺最多铺几张。再多，每张就只剩一条缝了，不如叠起来。
constexpr int kMaxTiled = 4;

// 卡片之间的缝。
constexpr int kGap = 10;

// 每张卡背后的影子：几层、每层往右下错开多少。
// 每张窗口卡都垫这么多层，像一叠窗口压在它下面，露出来的是右下那一圈。
constexpr int kShadowLayers = 4;
constexpr int kShadowStep = 10;
constexpr int kShadowSpan = kShadowLayers * kShadowStep;

// 鼠标压到外露的影子上时，第一张往上提多少、用多久。
// 10px 够看出"它动了"，又不至于把上面那条边距吃掉（边距正好 12）。
constexpr int kLiftHeight = 10;
constexpr int kLiftDuration = 140;

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
    return static_cast<quintptr>(window()->winId());
}

QRect WindowCard::screenRect() const {
    return QRect(mapToGlobal(QPoint(0, 0)), size());
}

QRect WindowCard::contentRect() const {
    if (m_compact || width() <= 24 || height() <= 66) return {};
    return {12, 12, width() - 24, height() - 66};
}

QRect WindowCard::previewRect() const {
    const auto content = contentRect();
    return content.isEmpty() ? QRect{} : QRect(mapTo(window(), content.topLeft()), content.size());
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

    // 提起来那一下：只有一个 0→1 的补间，改的是第一张的 y，不参与重排。
    // 用 QVariantAnimation 而不是给 CardStack 加 Q_PROPERTY：
    // 提起不是控件的状态，是"这一下动作"的进度，没必要变成可绑定的属性。
    m_liftAnim = new QVariantAnimation(this);
    m_liftAnim->setDuration(kLiftDuration);
    m_liftAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_liftAnim->setStartValue(0.0);
    m_liftAnim->setEndValue(1.0);
    connect(m_liftAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_lift = value.toReal();
        applyLift();
    });
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

    // ---- 每张卡背后的影子界面 ----
    // 每张窗口卡都垫 kShadowLayers 层圆角板，每层往右下错开 10px：像一叠窗口压在
    // 它下面，露出来的是右下那一圈。画在父控件上，子卡片盖住的部分自动看不见。
    // 第一张的影子按"落位"(m_shadowBase)画：它被提起来时影子留在原地，才看得出"提"；
    // 其余卡按各自几何画。
    if (m_shadowLayers > 0 && !m_stacked) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (int c = 0; c < m_cards.size(); ++c) {
            const QRect base = (c == 0 && !m_shadowBase.isNull())
                                   ? m_shadowBase
                                   : m_cards.at(c)->geometry();
            if (base.isNull() || base.width() <= 0 || base.height() <= 0) {
                continue;
            }
            // 由远及近画：最远的最暗、也最靠右下，近的压在它上面。
            // 第一张被提起来时整体提亮一点：离桌面远了，影子该淡一些。
            const int liftTone = (c == 0) ? qRound(m_lift * 6) : 0;
            for (int i = m_shadowLayers; i >= 1; --i) {
                const int offset = i * kShadowStep;
                QPainterPath layer;
                layer.addRoundedRect(QRectF(base).translated(offset, offset), kCardRadius,
                                     kCardRadius);
                const int tone = 0x21 + (m_shadowLayers - i) * 7 + liftTone;
                painter.fillPath(layer, QColor(tone, tone + 5, tone + 11));
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(255, 255, 255, 26), 1));
                painter.drawPath(layer);
            }
        }
    }

    painter.setPen(QColor(0x24, 0x28, 0x30));
    painter.drawLine(width() - 1, 0, width() - 1, height());
}

void CardStack::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    // The first release may already have promoted/repositioned the card.
    // Retain its identity instead of hitting a different card at the old point.
    const QPoint delta = event->pos() - m_lastClickPos;
    const int distance = QApplication::styleHints()->mouseDoubleClickDistance();
    if (m_lastClickId && m_lastClickTimer.isValid() &&
        m_lastClickTimer.elapsed() <= QApplication::doubleClickInterval() &&
        qAbs(delta.x()) <= distance && qAbs(delta.y()) <= distance) {
        const WindowId id = m_lastClickId;
        m_lastClickId = 0;
        for (const auto* card : m_cards) {
            if (card->windowId() == id) {
                emit windowActivated(id);
                return;
            }
        }
        return;  // The clicked window disappeared; never activate its replacement.
    }
    const int index = cardIndexAt(event->pos());
    if (index >= 0) {
        emit windowActivated(m_cards.at(index)->windowId());
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

int CardStack::shadowSpan() const {
    return m_shadowLayers > 0 ? kShadowSpan : 0;
}

// 把第 index 张放到 target：动画版和直给版只差这一处，抽出来省得写三遍。
void CardStack::placeCard(int index, const QRect& target, bool animated) {
    WindowCard* card = m_cards.at(index);
    for (auto* anim : card->findChildren<QPropertyAnimation*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (anim->propertyName() == "geometry") { anim->stop(); anim->deleteLater(); }
    }
    if (animated && card->geometry() != target) {
        auto* anim = new QPropertyAnimation(card, "geometry", card);
        anim->setDuration(170);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setStartValue(card->geometry());
        anim->setEndValue(target);
        // 影子是按"落位"画的：卡片动的时候影子不跟着动，但它被卡片盖住/露出的
        // 那部分要重画，否则卡片滑回去之后影子还缺一块。
        connect(anim, &QPropertyAnimation::valueChanged, this, [this] { update(); });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        card->setGeometry(target);
    }
}

void CardStack::relayout(bool animated) {
    // 重排就是"重新落位"：抬起来的那张先放回去，否则算出来的落位和实际几何对不上。
    clearLift();

    const int count = m_windows.size();
    if (count == 0) {
        m_shadowLayers = 0;
        m_shadowBase = QRect();
        update();
        return;
    }

    // 四张以内平铺（四张都看得见）；五张起平铺就只剩一条缝，改成带标题的叠影。
    m_stacked = count > kMaxTiled;
    m_compact = false;

    // 上下左右都留 12px：贴着边缘会被切掉圆角和边框，看起来像溢出。
    const int inner = this->height() - kMargin * 2;
    const int full = this->width() - kMargin * 2;

    if (m_stacked) {
        const int maxCard = qMin(280, inner / 2);
        int step = qMax(kTextHeight, qMin(inner * 22 / 100, (inner - maxCard) / (count - 1)));
        int cardHeight = inner - (count - 1) * step;

        // 连一条标题都排不下（大约 20 张以上）：不再叠影，退化成一条条标题。
        m_compact = cardHeight < kTextHeight;
        if (m_compact) {
            step = qMax(1, inner / count);
            cardHeight = step;
        }

        // 每深一张往右错一点；横向总量压在栏宽的 40% 以内，压缩档不斜。
        m_shift = m_compact ? 0 : qBound(4, full * 40 / 100 / (count - 1), 20);
        m_step = step;
        m_shadowLayers = 0;
        m_shadowBase = QRect();

        for (int i = 0; i < count; ++i) {
            const QRect target(kMargin + i * m_shift, kMargin + i * step, full - i * m_shift,
                               cardHeight);
            placeCard(i, target, animated);
            WindowCard* card = m_cards.at(i);
            card->setCompact(m_compact);
            card->setCoverEdge(m_compact ? 0 : cardHeight - step);
            card->setDepth(m_compact ? 0 : i);
        }
    } else {
        // 平铺：几张等分高度，四张一样大，从上到下把整条栏填满。
        // 影子往右下走，所以右边要让出一条 kShadowSpan 给它；下边不用让——
        // 第一张下面隔一条缝就是第二张，影子的下半截本来就压在第二张底下。
        // 让出来的宽度是几张一起让的——只让第一张，它就比别的窄一圈，面积就不一样了。
        m_shift = 0;
        m_shadowLayers = count > 1 ? kShadowLayers : 0;
        const int span = m_shadowLayers > 0 ? kShadowSpan : 0;
        const int width = full - span;
        const int top = kMargin;
        // 高除以份数多半除不尽，剩下的 1–3px 摊进卡片之间的缝里。
        // 摊到某一张的高度上会让四张不一般大，留在底部又会看出来没铺满。
        const int usable = this->height() - kMargin * 2;
        const int cardHeight =
            qMax(60, (usable - kGap * (count - 1)) / count);
        const int leftover = qMax(0, usable - cardHeight * count - kGap * (count - 1));
        m_step = cardHeight + kGap;

        int y = top;
        for (int i = 0; i < count; ++i) {
            const QRect target(kMargin, y, width, cardHeight);
            placeCard(i, target, animated);
            WindowCard* card = m_cards.at(i);
            card->setCompact(false);
            card->setOverlapped(false);
            card->setCoverEdge(0);
            card->setDepth(0);
            y += cardHeight + kGap + (i < leftover ? 1 : 0);
        }
        // 影子按"第一张的落位"画，不按它当前的位置：拖动时卡片跑开、影子留在原地，
        // 提起来的那一下也一样——卡片往上走、影子不动，才看得出是"提"。
        m_shadowBase = QRect(kMargin, top, width, cardHeight);
    }

    // i=0 最后 raise，所以它压在最上面：视觉上"第一张在最前面"。
    for (int i = count - 1; i >= 0; --i) {
        m_cards.at(i)->raise();
    }
    update();
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

    // 点在影子界面上：那不是另一个窗口，是第一张卡垫出来的厚度，算点第一张。
    if (m_shadowLayers > 0 && !m_shadowBase.isNull()) {
        const QRect shadow = m_shadowBase.adjusted(0, 0, kShadowSpan, kShadowSpan);
        if (shadow.contains(pos)) {
            return 0;
        }
    }
    return -1;
}

// 外露的那条影子：第一张右边、影子还没有被卡片盖住的那一圈。
// 上半截从第一层影子开始（再往上那 10px 是空的），下沿连到影子的最外一层——
// 右下角那一段虽然在第二张旁边，但它也属于这一摞的边，一起算抓手。
bool CardStack::onShadowEdge(const QPoint& pos) const {
    if (m_stacked || m_shadowLayers <= 0 || m_shadowBase.isNull()) {
        return false;
    }
    const QRect band(m_shadowBase.right() + 1, m_shadowBase.top() + kShadowStep, kShadowSpan,
                     m_shadowBase.height() - kShadowStep + kShadowSpan);
    return band.contains(pos);
}

void CardStack::updateHover(const QPoint& pos) {
    if (m_dragging) {
        return;
    }
    const bool onEdge = onShadowEdge(pos);
    if (onEdge == m_hoverEdge) {
        return;
    }
    m_hoverEdge = onEdge;
    setCursor(onEdge ? Qt::OpenHandCursor : Qt::ArrowCursor);
    setLiftTarget(onEdge);
}

void CardStack::setLiftTarget(bool on) {
    if (!m_liftAnim) {
        return;
    }
    if (on && !m_cards.isEmpty()) {
        for (auto* anim : m_cards.first()->findChildren<QPropertyAnimation*>(
                 QString(), Qt::FindDirectChildrenOnly)) {
            if (anim->propertyName() == "geometry") { anim->stop(); anim->deleteLater(); }
        }
    }
    m_liftAnim->stop();
    m_liftAnim->setStartValue(m_lift);
    m_liftAnim->setEndValue(on ? 1.0 : 0.0);
    m_liftAnim->start();
}

// 把"提起进度"落到几何上：只有 y 变，尺寸一点不改——四张还是同样大。
void CardStack::applyLift() {
    if (m_cards.isEmpty() || m_shadowLayers <= 0 || m_shadowBase.isNull() || m_dragging) {
        return;
    }
    WindowCard* first = m_cards.first();
    const QRect target = m_shadowBase.translated(0, -qRound(m_lift * kLiftHeight));
    if (first->geometry() != target) {
        first->setGeometry(target);
    }
    update();  // 卡片让开的地方要露出影子，得跟着重画
}

// 重排前必须先放回去：这时候的 m_lift 已经没有意义了。
void CardStack::clearLift() {
    if (m_liftAnim) {
        m_liftAnim->stop();
    }
    m_lift = 0.0;
    m_hoverEdge = false;
    unsetCursor();
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
    m_lastClickId = 0;
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
        updateHover(event->pos());
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!m_dragging && (event->pos() - m_pressPos).manhattanLength() > 6) {
        m_dragging = true;
        m_liftAnim->stop();
        for (auto* anim : m_cards.at(m_pressIndex)->findChildren<QPropertyAnimation*>(
                 QString(), Qt::FindDirectChildrenOnly)) {
            if (anim->propertyName() == "geometry") { anim->stop(); anim->deleteLater(); }
        }
        // 拖动时先把它提到最上面，跟着鼠标走，才看得出"被抽出来了"。
        m_cards.at(m_pressIndex)->raise();
        setCursor(Qt::ClosedHandCursor);
    }

    if (m_dragging) {
        WindowCard* card = m_cards.at(m_pressIndex);
        QPoint target = event->pos() + m_dragOffset;
        target.setY(qBound(0, target.y(), height() - card->height()));
        card->move(target);
        update();  // 卡片让开之后，露出来的影子要重画
    }
}

void CardStack::leaveEvent(QEvent* event) {
    // 鼠标离开整条栏：提起来的那张落回去，不然它会一直悬在半空。
    if (!m_dragging) {
        m_hoverEdge = false;
        setLiftTarget(false);
        unsetCursor();
    }
    QWidget::leaveEvent(event);
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
    if (!dragged) {
        m_lastClickId = id;
        m_lastClickPos = event->pos();
        m_lastClickTimer.restart();
    }

    if (m_stacked && index > 0) {
        promote(index);  // 叠影里点一下或拖出来，都把它抽到最前面
    } else {
        relayout(true);  // 平铺：拖出来松手后滑回原来的位置
    }

    emit windowClicked(id);
}

}  // namespace crossdesk::ui
