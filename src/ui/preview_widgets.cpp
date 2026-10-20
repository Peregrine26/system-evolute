#include "preview_widgets.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QResizeEvent>

#include <algorithm>

namespace crossdesk::ui {

namespace {

// 窗口卡片四周留给投影的空白。投影画在自己的矩形里，才不会盖到邻居身上。
constexpr int kShadowPad = 8;

// 画布用的深色，和主窗口的浅色任务栏形成"桌面 / 应用"两层关系。
const QColor kWindowBody(0x25, 0x29, 0x32);
const QColor kWindowBar(0x2f, 0x34, 0x3f);
const QColor kWindowContent(0x1b, 0x1f, 0x26);
const QColor kAccent(0x0a, 0x84, 0xff);

// 样式表里写的是 font-size:13px，这时 pointSizeF() 返回 -1。
// 直接 setPointSizeF(-1 + 1.5) 会得到一个无效字号，文字会被省略成 "…"，
// 所以升级字号必须按像素来算。
QFont biggerFont(const QFont& base, int deltaPixels) {
    QFont result = base;
    if (result.pixelSize() > 0) {
        result.setPixelSize(result.pixelSize() + deltaPixels);
    } else {
        result.setPointSizeF(result.pointSizeF() + deltaPixels * 0.75);
    }
    return result;
}

}  // namespace

ThumbnailSlot::ThumbnailSlot(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);  // 点窗口不该拦住底下的操作
}

void ThumbnailSlot::setWindow(const WindowItem* window) {
    if (!window) {
        if (m_id == 0) {
            return;
        }
        m_id = 0;
        m_title.clear();
        m_process.clear();
        m_preview = QPixmap();
        update();
        return;
    }

    // 同一个窗口重画时不要丢掉已经拿到的缩略图：A 注册一次不容易，
    // 每次刷新都清空会闪成占位态。
    if (m_id == window->id) {
        m_title = window->title;
        m_process = window->processName;
        update();
        return;
    }

    m_id = window->id;
    m_title = window->title;
    m_process = window->processName;
    m_preview = QPixmap();  // 换了窗口，旧画面必须丢掉
    update();
}

void ThumbnailSlot::setBadge(const QString& text) {
    if (m_badge == text) {
        return;
    }
    m_badge = text;
    update();
}

void ThumbnailSlot::setHighlighted(bool on) {
    if (m_highlighted == on) {
        return;
    }
    m_highlighted = on;
    update();
}

void ThumbnailSlot::setPreview(const QPixmap& pixmap) {
    m_preview = pixmap;
    update();
}

void ThumbnailSlot::clearPreview() {
    m_preview = QPixmap();
    update();
}

quintptr ThumbnailSlot::hostHandle() {
    return static_cast<quintptr>(winId());
}

QRect ThumbnailSlot::screenRect() const {
    return QRect(mapToGlobal(QPoint(0, 0)), size());
}

void ThumbnailSlot::paintEvent(QPaintEvent*) {
    if (m_id == 0) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 阴影占掉一圈内边距；窗口越小时阴影越窄，免得把窗口本体挤没了。
    const int pad = qBound(0, qMin(width(), height()) / 8, kShadowPad);
    const QRectF box = QRectF(rect()).adjusted(pad + 0.5, pad + 0.5, -pad - 0.5, -pad - 0.5);
    if (box.width() < 6 || box.height() < 6) {
        painter.fillRect(rect(), kWindowBody);
        return;
    }

    // ---- 投影：让叠在一起的窗口能分清前后 ----
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 30));
    painter.drawRoundedRect(box.adjusted(1, 4, 1, 5), 7, 7);
    painter.setBrush(QColor(0, 0, 0, 22));
    painter.drawRoundedRect(box.adjusted(0, 1, 0, 2), 7, 7);

    QPainterPath path;
    path.addRoundedRect(box, 6, 6);
    painter.fillPath(path, kWindowBody);

    painter.save();
    painter.setClipPath(path);

    // ---- 标题栏 ----
    const qreal barHeight = qBound(0.0, box.height() * 0.22, 26.0);
    const bool hasBar = barHeight >= 9;

    if (hasBar) {
        painter.fillRect(QRectF(box.left(), box.top(), box.width(), barHeight), kWindowBar);

        const bool roomForDots = box.width() > 96 && barHeight >= 13;
        if (roomForDots) {
            const qreal radius = qMax(2.0, qMin(3.0, barHeight / 8.0));
            const QColor dots[3] = {QColor(0xff, 0x5f, 0x57), QColor(0xfe, 0xbc, 0x2e),
                                    QColor(0x28, 0xc8, 0x40)};
            for (int i = 0; i < 3; ++i) {
                painter.setBrush(dots[i]);
                painter.drawEllipse(
                    QPointF(box.left() + 10 + i * (radius * 3.2), box.top() + barHeight / 2),
                    radius, radius);
            }
        }

        if (box.width() > 130) {
            QFont titleFont = font();
            titleFont.setPixelSize(qBound(9, static_cast<int>(barHeight) - 10, 11));
            painter.setFont(titleFont);
            painter.setPen(QColor(0xe6, 0xe8, 0xec, 220));
            const int textLeft = roomForDots ? 42 : 10;
            const QRectF textRect(box.left() + textLeft, box.top(),
                                  box.width() - textLeft - 8, barHeight);
            const QFontMetrics metrics(titleFont);
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(m_title.isEmpty() ? QStringLiteral("（无标题）")
                                                                  : m_title,
                                                Qt::ElideRight,
                                                static_cast<int>(textRect.width())));
        }
    }

    // ---- 内容区 ----
    const QRectF content(box.left(), box.top() + (hasBar ? barHeight : 0), box.width(),
                         box.height() - (hasBar ? barHeight : 0));
    painter.fillRect(content, kWindowContent);

    if (!m_preview.isNull()) {
        // A 的 DWM 内容来了就直接铺满，保持比例、居中。
        const QPixmap scaled =
            m_preview.scaled(content.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawPixmap(content.center().toPoint() - QPoint(scaled.width() / 2, scaled.height() / 2),
                           scaled);
    } else if (content.height() > 24 && content.width() > 64) {
        // 占位：左边一条窄侧栏 + 右边几行短横线。
        // 比一排通栏长条更像真实应用（编辑器 / 文件管理器），
        // 又不会假装这里已经有内容。
        painter.setPen(Qt::NoPen);
        const qreal sidebar = content.width() * 0.16;
        painter.setBrush(QColor(255, 255, 255, 6));
        painter.drawRect(QRectF(content.left(), content.top(), sidebar, content.height()));

        const qreal lineLeft = content.left() + sidebar + content.width() * 0.06;
        const qreal maxWidth = content.width() - sidebar - content.width() * 0.12;
        for (int i = 0; i < 4; ++i) {
            const qreal y = content.top() + content.height() * (0.14 + i * 0.2);
            if (y + 8 > content.bottom()) {
                break;
            }
            const qreal widthRatio = (i % 3 == 2) ? 0.55 : 0.92;
            painter.setBrush(QColor(255, 255, 255, 12));
            painter.drawRoundedRect(
                QRectF(lineLeft, y, maxWidth * widthRatio,
                       qBound(2.0, content.height() * 0.035, 6.0)),
                2, 2);
        }
    }

    painter.restore();

    // ---- 边框 / 选中高亮 ----
    painter.setBrush(Qt::NoBrush);
    if (m_highlighted) {
        painter.setPen(QPen(kAccent, 2.4));
    } else {
        painter.setPen(QPen(QColor(255, 255, 255, 34), 1.0));
    }
    painter.drawPath(path);

    // ---- 角标（排列模式的"左 / 右"）----
    if (!m_badge.isEmpty() && box.width() > 64 && box.height() > 34) {
        QFont badgeFont = font();
        badgeFont.setPixelSize(10);
        badgeFont.setBold(true);
        const QFontMetrics metrics(badgeFont);
        const qreal badgeWidth = metrics.horizontalAdvance(m_badge) + 12;
        const QRectF badge(box.right() - badgeWidth - 5, box.top() + 5, badgeWidth, 15);
        painter.setPen(Qt::NoPen);
        painter.setBrush(kAccent);
        painter.drawRoundedRect(badge, 7.5, 7.5);
        painter.setFont(badgeFont);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, m_badge);
    }
}

LayoutPreview::LayoutPreview(QWidget* parent) : QWidget(parent) {
    setMinimumSize(240, 180);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void LayoutPreview::showDesktop(const QRect& workArea,
                                const QVector<WindowItem>& windows,
                                const QVector<WindowId>& highlighted) {
    m_workArea = workArea;
    m_highlighted = highlighted;
    m_badges.clear();

    // 只画"知道自己在哪里"的窗口。没有位置就画不出重叠关系，
    // 硬塞一个默认矩形只会误导用户。
    QVector<WindowItem> visible;
    visible.reserve(windows.size());
    for (const WindowItem& window : windows) {
        if (window.hasBounds) {
            visible.push_back(window);
        }
    }

    // 面积大的先画（在底层），小的压在上面——和真实桌面里"小窗口浮在大窗口上"
    // 的观感一致。面积相同时按 id 排，保证每次重画顺序稳定，不会自己闪。
    std::stable_sort(visible.begin(), visible.end(), [](const WindowItem& a, const WindowItem& b) {
        const qint64 areaA = qint64(a.bounds.width()) * a.bounds.height();
        const qint64 areaB = qint64(b.bounds.width()) * b.bounds.height();
        if (areaA != areaB) {
            return areaA > areaB;
        }
        return a.id < b.id;
    });

    m_items = visible;
    m_rects.clear();
    m_rects.reserve(visible.size());
    for (const WindowItem& window : visible) {
        m_rects.push_back(window.bounds);
    }

    updateFrame();
    applyLayout();
    update();
}

void LayoutPreview::showArrangement(const QRect& workArea,
                                    const QVector<Placement>& placements,
                                    const QVector<WindowItem>& windows) {
    m_workArea = workArea;
    m_highlighted.clear();
    m_items.clear();
    m_rects.clear();
    m_badges.clear();

    for (int i = 0; i < placements.size(); ++i) {
        const Placement& placement = placements.at(i);
        for (const WindowItem& window : windows) {
            if (window.id != placement.id) {
                continue;
            }
            m_items.push_back(window);
            m_rects.push_back(placement.rect);
            m_badges.push_back(placements.size() > 1 ? (i == 0 ? QStringLiteral("左")
                                                               : QStringLiteral("右"))
                                                     : QString());
            break;
        }
    }

    updateFrame();
    applyLayout();
    update();
}

void LayoutPreview::clear() {
    m_items.clear();
    m_rects.clear();
    m_badges.clear();
    m_highlighted.clear();
    hideAllPanes();
    update();
}

int LayoutPreview::paneCount() const {
    int count = 0;
    for (ThumbnailSlot* pane : m_panes) {
        if (pane->isVisible()) {
            ++count;
        }
    }
    return count;
}

void LayoutPreview::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateFrame();
    applyLayout();
}

void LayoutPreview::updateFrame() {
    QRect area = m_workArea;
    if (!area.isValid() || area.width() <= 0 || area.height() <= 0) {
        area = QRect(0, 0, 1920, 1080);  // 还没拿到真实可用区域时的示意比例
    }

    if (width() <= 40 || height() <= 40) {
        m_frame = QRect();
        return;
    }

    const qreal scaleX = static_cast<qreal>(width()) / area.width();
    const qreal scaleY = static_cast<qreal>(height()) / area.height();
    m_area = area;

    // 左边那条任务栏吃掉了画布宽度，横向缩放必然小于纵向，这是几何事实。
    // 所以默认就按两个方向各自的缩放把画面铺满：用户要的是"整个桌面都在这里"，
    // 四周留一大圈底色比轻微变形更像"没渲染完"。左右占比在横向，
    // 也就是这个界面唯一要说的信息，完全没有失真。
    //
    // 只有画布被挤成极窄一条时（竖屏、窗口被硬拽窄）才退回等比，
    // 那种情况下铺满会把窗口拉成面条，更没法看。
    if (scaleY / scaleX <= 2.2) {
        m_scaleX = scaleX;
        m_scaleY = scaleY;
        m_frame = rect();
        return;
    }

    // 画布实在太窄（竖屏、超宽屏）时退回等比，宁可有边也不要拉成面条。
    const qreal scale = qMin(scaleX, scaleY);
    m_scaleX = scale;
    m_scaleY = scale;
    m_frame = QRect((width() - qRound(area.width() * scale)) / 2,
                    (height() - qRound(area.height() * scale)) / 2,
                    qRound(area.width() * scale), qRound(area.height() * scale));
}

void LayoutPreview::applyLayout() {
    if (m_frame.isEmpty()) {
        hideAllPanes();
        return;
    }

    while (m_panes.size() < m_items.size()) {
        m_panes.push_back(new ThumbnailSlot(this));
    }

    for (int i = 0; i < m_panes.size(); ++i) {
        ThumbnailSlot* pane = m_panes.at(i);
        if (i >= m_items.size()) {
            pane->hide();
            continue;
        }

        const WindowItem& item = m_items.at(i);
        const QRect target = m_rects.at(i);

        // 目标矩形 -> 画布坐标。窗口本体要在投影内缩进之后仍落在 target 上，
        // 所以这里把几何按投影宽度外扩一圈。
        const int pad = qBound(0, qMin(target.width(), target.height()) / 6, kShadowPad);
        const QRect mapped(
            m_frame.left() + qRound((target.left() - m_area.left()) * m_scaleX),
            m_frame.top() + qRound((target.top() - m_area.top()) * m_scaleY),
            qRound(target.width() * m_scaleX), qRound(target.height() * m_scaleY));
        pane->setGeometry(mapped.adjusted(-pad, -pad, pad, pad));

        pane->setWindow(&item);
        pane->setBadge(m_badges.value(i));
        pane->setHighlighted(m_highlighted.contains(item.id));
        pane->show();
        pane->raise();  // 按下标顺序叠放：靠后的在上面
    }
}

void LayoutPreview::hideAllPanes() {
    for (ThumbnailSlot* pane : m_panes) {
        pane->hide();
    }
}

void LayoutPreview::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // ---- 桌面底：整块铺满，不留白 ----
    QLinearGradient backdrop(0, 0, 0, height());
    backdrop.setColorAt(0.0, QColor(0x24, 0x2a, 0x36));
    backdrop.setColorAt(0.55, QColor(0x18, 0x1c, 0x24));
    backdrop.setColorAt(1.0, QColor(0x11, 0x14, 0x1a));
    painter.fillRect(rect(), backdrop);

    QRadialGradient glow(QPointF(width() * 0.32, height() * 0.12), width() * 0.85);
    glow.setColorAt(0.0, QColor(70, 120, 190, 34));
    glow.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.fillRect(rect(), glow);

    // ---- 屏幕范围：铺满时不需要边界，窄画布退回等比时才画一圈极淡的框 ----
    if (!m_frame.isEmpty() && m_frame != rect()) {
        const QRectF frame = QRectF(m_frame).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(frame, 14, 14);
        painter.fillPath(path, QColor(255, 255, 255, 6));
        painter.setPen(QPen(QColor(255, 255, 255, 18), 1));
        painter.drawPath(path);
    }

    if (m_items.isEmpty()) {
        painter.setPen(QColor(255, 255, 255, 64));
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("没有可管理的窗口"));
    }
}

}  // namespace crossdesk::ui
