#include "window_cards.h"

#include <QEasingCurve>
#include <QApplication>
#include <QStyleHints>
#include <QContextMenuEvent>
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

void WindowCard::setSecondary(bool on) {
    if (m_secondary == on) {
        return;
    }
    m_secondary = on;
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
    if (m_compact || width() <= 4 || height() <= 4) return {};
    // 画面区填满整张卡，只留 1px 给边框。
    return {1, 1, width() - 2, height() - 2};
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

    // ---- 画面区：填满整张卡，不再留标题条/进程名 ----
    const QRectF screen = QRectF(0, 0, width(), height());
    QPainterPath screenPath;
    screenPath.addRoundedRect(screen, kCardRadius, kCardRadius);
    painter.fillPath(screenPath, QColor(0x15, 0x18, 0x1e));

    if (!m_preview.isNull()) {
        painter.save();
        painter.setClipPath(screenPath);
        const QPixmap scaled =
            m_preview.scaled(screen.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        painter.drawPixmap(screen.topLeft().toPoint(), scaled);
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

    // ---- 选中：蓝边（不再画标题条/圆点）----
    painter.setBrush(Qt::NoBrush);
    if (m_selected) {
        painter.setPen(QPen(kAccent, 2.0));
    } else if (m_secondary) {
        painter.setPen(QPen(QColor(0x5a, 0x9e, 0xff), 1.6));  // 第二选中：浅蓝
    } else {
        painter.setPen(QPen(QColor(255, 255, 255, 20), 1.0));
    }
    painter.drawPath(cardPath);

    // 第二选中：左上角画"2"角标，和主选中区分
    if (m_secondary) {
        const QRectF badge(4, 4, 18, 18);
        QPainterPath bp;
        bp.addRoundedRect(badge, 4, 4);
        painter.fillPath(bp, QColor(0x5a, 0x9e, 0xff));
        QFont bf = font();
        bf.setPixelSize(11);
        bf.setBold(true);
        painter.setFont(bf);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, QStringLiteral("2"));
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
        card->setSecondary(m_windows.at(i).id == m_secondary);
        card->show();
    }
}

void CardStack::setSelected(WindowId id) {
    m_selected = id;
    for (WindowCard* card : m_cards) {
        card->setSelected(card->windowId() == id);
    }
}

void CardStack::setSecondarySelected(WindowId id) {
    m_secondary = id;
    for (WindowCard* card : m_cards) {
        card->setSecondary(card->windowId() == id);
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

    // 装饰影子不再画：同格内叠放的卡片是真实窗口（有预览内容），
    // 露出的右下边自然形成层次，不需要额外画影子色块。
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
    return m_span;
}

// 右键窗口：发信号给主窗口，展开该格子堆叠的 Task View。
// 每张卡的"本体 + 它背后同格卡片错开露出的区域"都算这张卡的格子。
// 用 m_span（实际错开量）而不是 kShadowSpan（固定 40），避免 4 窗口无错开时
// 命中区域往下扩 40px 侵入下一格，右键下一格上半部分误命中上一格。
void CardStack::contextMenuEvent(QContextMenuEvent* event) {
    const QPoint pos = event->pos();
    int index = -1;
    for (int i = 0; i < m_cards.size(); ++i) {
        const QRect r = m_cards.at(i)->geometry();
        if (r.contains(pos) || r.adjusted(0, 0, m_span, m_span).contains(pos)) {
            index = i;
            break;
        }
    }
    if (index >= 0 && index < m_cards.size()) {
        emit windowContextMenu(m_cards.at(index)->windowId());
    }
    event->accept();
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
        m_span = 0;
        update();
        return;
    }

    // 4 格分组 + 每格内分层：窗口 i 放在格子 (i % slotCount) 的第 (i / slotCount) 层。
    // 前 4 个各占一格（最前面），第 5-8 分别叠在第 1-4 后面，第 9-12 再叠一层，以此类推。
    // 影子层是真实窗口卡片（有预览内容、能交互），不是装饰色块。
    m_stacked = false;
    m_compact = false;
    const int slotCount = qMin(count, kMaxTiled);            // 格子数（最多 4）
    const int maxLayers = (count + slotCount - 1) / slotCount;   // 最深层数
    const int span = (maxLayers - 1) * kShadowStep;      // 影子总错开量
    m_shift = 0;
    m_step = kShadowStep;
    m_shadowLayers = 0;   // 装饰影子不画了（影子是真实卡片，自己画内容）
    m_span = span;        // 同格内卡片往右下错开的总量

    const int inner = this->height() - kMargin * 2;
    const int full = this->width() - kMargin * 2;
    const int width = full - span;  // 让出右边给影子层错开
    const int usable = inner;
    const int cardHeight = qMax(60, (usable - kGap * (slotCount - 1)) / slotCount);
    const int leftover = qMax(0, usable - cardHeight * slotCount - kGap * (slotCount - 1));

    for (int i = 0; i < count; ++i) {
        const int g = i % slotCount;   // 格子
        const int L = i / slotCount;   // 层（0 = 最前）
        const int y = kMargin + g * (cardHeight + kGap) + (g < leftover ? 1 : 0);
        const QRect target(kMargin + L * kShadowStep, y + L * kShadowStep, width, cardHeight);
        placeCard(i, target, animated);
        WindowCard* card = m_cards.at(i);
        card->setCompact(false);
        card->setOverlapped(L > 0);
        card->setCoverEdge(0);
        card->setDepth(L);
    }

    // 记下每张卡的落位：悬停上提从落位往上抬，影子按落位画（卡片动时影子不动）。
    m_restGeo.clear();
    for (int i = 0; i < m_cards.size(); ++i) {
        m_restGeo.append(m_cards.at(i)->geometry());
    }

    // z-order：同格内层 0 最上（最后 raise）。层深的先 raise（压低），层 0 最后 raise。
    for (int L = maxLayers - 1; L >= 0; --L) {
        for (int g = 0; g < slotCount; ++g) {
            const int i = L * slotCount + g;
            if (i < count) {
                m_cards.at(i)->raise();
            }
        }
    }
    update();
}

int CardStack::cardIndexAt(const QPoint& pos) const {
    const int count = m_cards.size();
    if (count == 0) return -1;
    const int slotCount = qMin(count, kMaxTiled);
    const int maxLayers = (count + slotCount - 1) / slotCount;
    // 层 0 优先（z 最上），同层格子任意顺序（不重叠）。
    for (int L = 0; L < maxLayers; ++L) {
        for (int g = 0; g < slotCount; ++g) {
            const int i = L * slotCount + g;
            if (i >= count) break;
            if (m_cards.at(i)->isVisible() && m_cards.at(i)->geometry().contains(pos)) {
                return i;
            }
        }
    }
    return -1;
}

// 外露的那条影子：第一张右边、影子还没有被卡片盖住的那一圈。
void CardStack::updateHover(const QPoint& pos) {
    if (m_dragging) {
        return;
    }
    // 悬停命中：同格内叠放的卡片是真实窗口，cardIndexAt 按 z 序（层 0 优先）
    // 遍历，落在后面卡片露出的右下边时会命中后面那张，自然提起。
    const int h = cardIndexAt(pos);
    if (h != m_hoveredCard) {
        m_hoveredCard = h;
        setLiftTarget(h >= 0);
    }
}

void CardStack::setLiftTarget(bool on) {
    if (!m_liftAnim) {
        return;
    }
    if (on && m_hoveredCard >= 0 && m_hoveredCard < m_cards.size()) {
        for (auto* anim : m_cards.at(m_hoveredCard)->findChildren<QPropertyAnimation*>(
                 QString(), Qt::FindDirectChildrenOnly)) {
            if (anim->propertyName() == "geometry") { anim->stop(); anim->deleteLater(); }
        }
    }
    m_liftAnim->stop();
    m_liftAnim->setStartValue(m_lift);
    m_liftAnim->setEndValue(on ? 1.0 : 0.0);
    m_liftAnim->start();
}

// 把"提起进度"落到几何上：只有 y 变，尺寸一点不改；提的是悬停的那张。
void CardStack::applyLift() {
    if (m_dragging || m_cards.isEmpty()) {
        return;
    }
    if (m_hoveredCard >= 0 && m_hoveredCard < m_cards.size()) {
        const QRect rest = m_restGeo.value(m_hoveredCard);
        if (!rest.isNull()) {
            const QRect target = rest.translated(0, -qRound(m_lift * kLiftHeight));
            WindowCard* card = m_cards.at(m_hoveredCard);
            if (card->geometry() != target) {
                card->setGeometry(target);
            }
        }
    } else if (m_lift < 0.01) {
        // 没有悬停的卡且提起进度归零：所有卡片落回各自落位
        for (int i = 0; i < m_cards.size() && i < m_restGeo.size(); ++i) {
            if (m_cards.at(i)->geometry() != m_restGeo.at(i)) {
                m_cards.at(i)->setGeometry(m_restGeo.at(i));
            }
        }
    }
    update();  // 卡片让开的地方要露出影子，得跟着重画
}

// 重排前必须先放回去：这时候的 m_lift 已经没有意义了。
void CardStack::clearLift() {
    if (m_liftAnim) {
        m_liftAnim->stop();
    }
    m_lift = 0.0;
    m_hoveredCard = -1;
    // 落回各自落位，别让谁悬在半空。
    for (int i = 0; i < m_cards.size() && i < m_restGeo.size(); ++i) {
        if (m_cards.at(i)->geometry() != m_restGeo.at(i)) {
            m_cards.at(i)->setGeometry(m_restGeo.at(i));
        }
    }
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
    // 鼠标离开整条栏：提起来的那张落回去。
    if (!m_dragging) {
        setLiftTarget(false);
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
