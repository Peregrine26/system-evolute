#include "window_row_delegate.h"

#include <QAbstractItemModel>
#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

namespace crossdesk::ui {

namespace {
constexpr int kCheckSize = 16;
constexpr int kPad = 16;
// 文字至少留这么宽，剩下的才轮到缩略图——窄屏上宁可缩略图小一点，
// 也不能把窗口标题挤成三个点。
constexpr int kTextMinWidth = 108;

QString elide(const QFontMetrics& metrics, const QString& text, int width) {
    return metrics.elidedText(text, Qt::ElideRight, qMax(0, width));
}

// 行内的小缩略图：一个窗口的轮廓，标题栏 + 两行内容。
// 不是真截图（DWM 还没接），但形状对，一眼能认出"这是一块画面"。
void paintThumbPlaceholder(QPainter* painter, const QRect& thumb, bool checked) {
    QPainterPath path;
    path.addRoundedRect(QRectF(thumb).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    painter->fillPath(path, QColor(0x1b, 0x1f, 0x26));
    painter->setPen(QPen(checked ? QColor(0x0a, 0x84, 0xff) : QColor(0x35, 0x3a, 0x44), 1));
    painter->drawPath(path);

    const QRect bar(thumb.left() + 1, thumb.top() + 1, thumb.width() - 2,
                    qMax(8, thumb.height() / 4));
    painter->fillRect(bar, QColor(0x2f, 0x34, 0x3f));

    painter->setPen(Qt::NoPen);
    for (int i = 0; i < 2; ++i) {
        const int y = bar.bottom() + 6 + i * qMax(5, bar.height() / 2 + 3);
        if (y + 3 > thumb.bottom()) {
            break;
        }
        painter->setBrush(QColor(255, 255, 255, 15));
        painter->drawRoundedRect(
            QRect(thumb.left() + 7, y, qRound((thumb.width() - 14) * (i == 0 ? 0.78 : 0.5)), 3), 1.5,
            1.5);
    }
}
}  // namespace

WindowRowDelegate::WindowRowDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize WindowRowDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex&) const {
    // 实际高度由 MainWindow 按"可用高度 / 窗口数"调 item->setSizeHint 分配，
    // 这里只给一个兜底值。
    return QSize(option.rect.width(), 84);
}

void WindowRowDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect row = option.rect;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;

    painter->fillRect(row, selected ? QColor(0x24, 0x28, 0x30) : QColor(0x1a, 0x1c, 0x21));
    if ((option.state & QStyle::State_MouseOver) != 0 && !selected) {
        painter->fillRect(row, QColor(255, 255, 255, 6));
    }
    painter->setPen(QColor(255, 255, 255, 14));
    painter->drawLine(QPoint(row.left() + kPad, row.bottom()), QPoint(row.right() - kPad, row.bottom()));

    // 勾选的行整行微微提亮，让人在深色列表里一眼看出"这两行被选中了"。
    if (checked) {
        painter->fillRect(QRect(row.left(), row.top(), 3, row.height()), QColor(0x0a, 0x84, 0xff));
    }

    // ---- 勾选框 ----
    const QRect checkRect(row.left() + kPad, row.center().y() - kCheckSize / 2, kCheckSize,
                          kCheckSize);
    QPainterPath boxPath;
    boxPath.addRoundedRect(checkRect, 4, 4);
    painter->fillPath(boxPath, checked ? QColor(0x0a, 0x84, 0xff) : QColor(0x25, 0x29, 0x31));
    painter->setPen(QPen(checked ? QColor(0x0a, 0x84, 0xff) : QColor(0x4a, 0x50, 0x5a), 1.2));
    painter->drawPath(boxPath);

    if (checked) {
        painter->setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPolygonF glyph;
        glyph << QPointF(checkRect.left() + 4, checkRect.center().y())
              << QPointF(checkRect.center().x(), checkRect.bottom() - 4)
              << QPointF(checkRect.right() - 3, checkRect.top() + 4);
        painter->drawPolyline(glyph);
    }

    // ---- 右侧缩略图：跟着行高一起长；行不够宽就自动缩小，不会顶掉标题 ----
    int thumbHeight = qBound(36, row.height() - 24, 120);
    int thumbWidth = qRound(thumbHeight * 16.0 / 9.0);
    const int maxThumbWidth = row.width() - kPad * 2 - kCheckSize - 14 - kTextMinWidth;
    if (thumbWidth > maxThumbWidth) {
        thumbWidth = qMax(0, maxThumbWidth);
        thumbHeight = qRound(thumbWidth * 9.0 / 16.0);
    }
    const bool showThumb = thumbWidth >= 44 && thumbHeight >= 26;

    const QRect thumbRect(row.right() - thumbWidth - kPad, row.center().y() - thumbHeight / 2,
                          thumbWidth, thumbHeight);
    if (showThumb) {
        paintThumbPlaceholder(painter, thumbRect, checked);
    }

    // ---- 文字：标题 / 进程名 / 失败原因（来自 item 的多行文本）----
    const QStringList lines = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n'));
    const int textLeft = checkRect.right() + 14;
    const int textRight = showThumb ? thumbRect.left() - 14 : row.right() - kPad;
    const int textWidth = textRight - textLeft;
    const bool hasFailure = lines.size() > 2;

    QFont titleFont = option.font;
    titleFont.setBold(true);
    titleFont.setPixelSize(14);
    QFont subFont = option.font;
    subFont.setPixelSize(12);

    const int titleHeight = 20;
    const int subHeight = 17;
    const int failureHeight = hasFailure ? 16 : 0;
    const int blockHeight = titleHeight + subHeight + failureHeight;
    int top = row.top() + (row.height() - blockHeight) / 2;

    painter->setFont(titleFont);
    painter->setPen(QColor(0xed, 0xee, 0xf1));
    painter->drawText(QRect(textLeft, top, textWidth, titleHeight),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      elide(QFontMetrics(titleFont), lines.value(0), textWidth));
    top += titleHeight;

    painter->setFont(subFont);
    painter->setPen(QColor(0x86, 0x8c, 0x96));
    painter->drawText(QRect(textLeft, top, textWidth, subHeight),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      elide(QFontMetrics(subFont), lines.value(1), textWidth));
    top += subHeight;

    if (hasFailure) {
        QString failure = lines.at(2);
        if (failure.startsWith(QStringLiteral("! "))) {
            failure = failure.mid(2);
        }
        painter->setPen(QColor(0xff, 0x6b, 0x60));
        painter->drawText(QRect(textLeft, top, textWidth, failureHeight),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          elide(QFontMetrics(subFont), failure, textWidth));
    }

    painter->restore();
}

bool WindowRowDelegate::editorEvent(QEvent* event,
                                    QAbstractItemModel* model,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index) {
    // 只有点在勾选框上才改变勾选状态，点整行的其它位置只是"选中这一行"。
    //
    // 为什么不做"点整行就勾选"：双击 = 定位，而双击会先后产生两次 Release。
    // 整行可勾选的话就是勾上又取消，用户看到的是"双击完全没反应"。
    // 勾选框的判定区域比画出来的方框大一圈，免得 16px 的小目标点不准。
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            const QRect row = option.rect;
            const QRect hit(row.left() + kPad - 8, row.center().y() - kCheckSize / 2 - 8,
                            kCheckSize + 16, kCheckSize + 16);
            if (hit.contains(mouse->pos())) {
                const Qt::CheckState next = index.data(Qt::CheckStateRole).toInt() == Qt::Checked
                                                ? Qt::Unchecked
                                                : Qt::Checked;
                model->setData(index, next, Qt::CheckStateRole);
                return true;
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

}  // namespace crossdesk::ui
