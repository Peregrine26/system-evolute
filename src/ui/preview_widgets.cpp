#include "preview_widgets.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace crossdesk::ui {

namespace {
constexpr int kPreviewMargin = 34;  // 画布四周留白，给"屏幕可用区域"标题腾位置
constexpr int kCorner = 10;

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
    setMinimumSize(160, 110);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
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

    m_id = window->id;
    m_title = window->title;
    m_process = window->processName;
    // 换了窗口，旧缩略图必须丢掉，否则会显示上一个窗口的画面。
    m_preview = QPixmap();
    update();
}

void ThumbnailSlot::setBadge(const QString& text) {
    if (m_badge == text) {
        return;
    }
    m_badge = text;
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
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(outer, kCorner, kCorner);

    painter.fillPath(path, QColor(QStringLiteral("#ffffff")));
    painter.setPen(QPen(QColor(m_id == 0 ? QStringLiteral("#e3e3e8") : QStringLiteral("#d2d2d7")),
                        1, m_id == 0 ? Qt::DashLine : Qt::SolidLine));
    painter.drawPath(path);

    if (m_id == 0) {
        painter.setPen(QColor(QStringLiteral("#86868b")));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("没有窗口"));
        return;
    }

    const QRect inner = rect().adjusted(14, 12, -14, -12);

    if (!m_preview.isNull()) {
        // A 的 DWM 内容来了就直接铺满，保持比例、居中。
        const QPixmap scaled =
            m_preview.scaled(inner.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawPixmap(inner.center() - QPoint(scaled.width() / 2, scaled.height() / 2),
                           scaled);
        return;
    }

    // ---- 占位态：M6 要求的降级显示 ----
    if (!m_badge.isEmpty()) {
        QFont badgeFont = biggerFont(font(), -1);
        const QFontMetrics badgeMetrics(badgeFont);
        const int badgeWidth = badgeMetrics.horizontalAdvance(m_badge) + 14;
        const QRect badgeRect(rect().right() - badgeWidth - 10, rect().top() + 10, badgeWidth, 20);
        QPainterPath badgePath;
        badgePath.addRoundedRect(badgeRect, 10, 10);
        painter.fillPath(badgePath, QColor(QStringLiteral("#0071e3")));
        painter.setFont(badgeFont);
        painter.setPen(QColor(QStringLiteral("#ffffff")));
        painter.drawText(badgeRect, Qt::AlignCenter, m_badge);
    }

    QFont titleFont = biggerFont(font(), 2);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(QStringLiteral("#1d1d1f")));

    const QString title = m_title.isEmpty() ? QStringLiteral("（无标题窗口）") : m_title;
    const QFontMetrics titleMetrics(titleFont);
    painter.drawText(QRect(inner.left(), inner.top() + 4, inner.width(), 24),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     titleMetrics.elidedText(title, Qt::ElideRight, inner.width()));

    painter.setFont(font());
    painter.setPen(QColor(QStringLiteral("#6e6e73")));
    const QFontMetrics metrics(font());
    painter.drawText(QRect(inner.left(), inner.top() + 32, inner.width(), 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(m_process, Qt::ElideRight, inner.width()));

    painter.setPen(QColor(QStringLiteral("#b0b0b5")));
    painter.drawText(QRect(inner.left(), inner.bottom() - 18, inner.width(), 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("实时缩略图未接入（等待 A 的 DWM）"));
}

LayoutPreview::LayoutPreview(QWidget* parent) : QWidget(parent) {
    m_left = new ThumbnailSlot(this);
    m_left->setBadge(QStringLiteral("左"));
    m_right = new ThumbnailSlot(this);
    m_right->setBadge(QStringLiteral("右"));
    m_left->hide();
    m_right->hide();
}

void LayoutPreview::setPlacements(const QRect& workArea,
                                  const QVector<Placement>& placements,
                                  const QVector<WindowItem>& windows) {
    m_workArea = workArea;
    m_placements = placements;
    m_windows = windows;

    if (m_placements.isEmpty()) {
        m_left->hide();
        m_right->hide();
        updateFrame();
        update();
        return;
    }

    const auto findWindow = [this](WindowId id) -> const WindowItem* {
        for (const WindowItem& window : m_windows) {
            if (window.id == id) {
                return &window;
            }
        }
        return nullptr;
    };

    m_left->setWindow(findWindow(m_placements.at(0).id));
    if (m_placements.size() > 1) {
        m_right->setWindow(findWindow(m_placements.at(1).id));
        m_right->show();
        m_left->setBadge(QStringLiteral("左"));
        m_right->setBadge(QStringLiteral("右"));
    } else {
        // 只勾了一个窗口：它占满整个可用区域，这时候没有左右之分。
        m_right->hide();
        m_left->setBadge(QString());
    }
    m_left->show();

    relayout();
    update();
}

void LayoutPreview::clear() {
    m_placements.clear();
    m_windows.clear();
    m_left->setWindow(nullptr);
    m_right->setWindow(nullptr);
    m_left->hide();
    m_right->hide();
    updateFrame();
    update();
}

void LayoutPreview::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateFrame();
    relayout();
}

void LayoutPreview::updateFrame() {
    // 屏幕框的尺寸只跟"可用区域"和控件大小有关，跟勾了几个窗口无关。
    // 之前把它写在 relayout() 里、且 relayout 在不满两个窗口时提前返回，
    // 结果空状态下 m_frame 是空矩形，整块区域什么都不画——看着像坏了。
    QRect area = m_workArea;
    if (!area.isValid() || area.width() <= 0 || area.height() <= 0) {
        area = QRect(0, 0, 1920, 1080);  // 还没拿到真实可用区域时的示意比例
    }

    const int availableWidth = width() - 2 * kPreviewMargin;
    const int availableHeight = height() - 2 * kPreviewMargin;
    if (availableWidth <= 40 || availableHeight <= 40) {
        m_frame = QRect();
        return;
    }

    // 等比缩放，且不放大到超过真实尺寸——否则在 4K 屏上预览会比真实窗口还大，
    // 反而看不出"这两个窗口各占多少"。
    qreal scale = qMin(static_cast<qreal>(availableWidth) / area.width(),
                       static_cast<qreal>(availableHeight) / area.height());
    scale = qMin(scale, 1.0);

    const int frameWidth = qRound(area.width() * scale);
    const int frameHeight = qRound(area.height() * scale);
    m_frame = QRect((width() - frameWidth) / 2, (height() - frameHeight) / 2, frameWidth,
                    frameHeight);
    m_scale = scale;
    m_area = area;
}

void LayoutPreview::relayout() {
    if (m_placements.isEmpty() || !m_area.isValid() || m_frame.isEmpty()) {
        return;
    }

    // 变量名不能叫 slots：Qt 把 slots 定义成了空宏（Q_OBJECT 的关键字），
    // 用了会得到一堆莫名其妙的 "语法错误: ["。
    const auto placeAt = [this](ThumbnailSlot* pane, const QRect& target) {
        const QRect mapped(m_frame.left() + qRound((target.left() - m_area.left()) * m_scale),
                           m_frame.top() + qRound((target.top() - m_area.top()) * m_scale),
                           qRound(target.width() * m_scale), qRound(target.height() * m_scale));
        pane->setGeometry(mapped.adjusted(2, 2, -2, -2));
    };

    placeAt(m_left, m_placements.at(0).rect);
    if (m_placements.size() > 1) {
        placeAt(m_right, m_placements.at(1).rect);
    }
}

void LayoutPreview::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_frame.isEmpty()) {
        return;
    }

    const QRectF frame = QRectF(m_frame).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(frame, 12, 12);
    // 桌面底色比页面略灰一点，屏幕才不会和背景糊成一片。
    painter.fillPath(path, QColor(QStringLiteral("#f5f5f7")));
    painter.setPen(QPen(QColor(QStringLiteral("#c7c7cc")), 1, Qt::DashLine));
    painter.drawPath(path);

    painter.setPen(QColor(QStringLiteral("#86868b")));
    painter.drawText(QRect(m_frame.left(), qMax(0, m_frame.top() - 26), m_frame.width(), 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("主屏幕可用区域 %1×%2（按比例缩小）")
                         .arg(m_area.width())
                         .arg(m_area.height()));

    if (m_placements.isEmpty()) {
        painter.drawText(m_frame, Qt::AlignCenter,
                         QStringLiteral("勾选左侧的任务，它的画面会按目标位置摆进这块屏幕"));
    }
}

}  // namespace crossdesk::ui
