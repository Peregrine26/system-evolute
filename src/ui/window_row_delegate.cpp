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
constexpr int kRowHeight = 78;
constexpr int kCheckSize = 16;
constexpr int kThumbWidth = 92;
constexpr int kThumbHeight = 56;
constexpr int kPad = 12;

QString elide(const QFontMetrics& metrics, const QString& text, int width) {
    return metrics.elidedText(text, Qt::ElideRight, qMax(0, width));
}
}  // namespace

WindowRowDelegate::WindowRowDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize WindowRowDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex&) const {
    return QSize(option.rect.width(), kRowHeight);
}

void WindowRowDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect row = option.rect.adjusted(0, 0, 0, -1);
    const bool selected = (option.state & QStyle::State_Selected) != 0;

    painter->fillRect(option.rect,
                      selected ? QColor(QStringLiteral("#eef4fd")) : QColor(QStringLiteral("#ffffff")));
    if ((option.state & QStyle::State_MouseOver) != 0 && !selected) {
        painter->fillRect(option.rect, QColor(0, 0, 0, 5));
    }
    painter->setPen(QColor(QStringLiteral("#f0f0f2")));
    painter->drawLine(row.bottomLeft(), row.bottomRight());

    // ---- 勾选框 ----
    const QRect checkRect(row.left() + kPad, row.center().y() - kCheckSize / 2, kCheckSize,
                          kCheckSize);
    const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
    QPainterPath boxPath;
    boxPath.addRoundedRect(checkRect, 4, 4);
    painter->fillPath(boxPath, checked ? QColor(QStringLiteral("#0071e3"))
                                       : QColor(QStringLiteral("#ffffff")));
    painter->setPen(QPen(checked ? QColor(QStringLiteral("#0071e3"))
                                 : QColor(QStringLiteral("#c7c7cc")),
                         1.2));
    painter->drawPath(boxPath);

    if (checked) {
        QPen tick(QColor(QStringLiteral("#ffffff")), 2, Qt::SolidLine, Qt::RoundCap,
                  Qt::RoundJoin);
        painter->setPen(tick);
        QPolygonF glyph;
        glyph << QPointF(checkRect.left() + 4, checkRect.center().y())
              << QPointF(checkRect.center().x(), checkRect.bottom() - 4)
              << QPointF(checkRect.right() - 3, checkRect.top() + 4);
        painter->drawPolyline(glyph);
    }

    // ---- 右侧缩略图 ----
    const QRect thumbRect(row.right() - kThumbWidth - kPad,
                          row.center().y() - kThumbHeight / 2, kThumbWidth, kThumbHeight);
    QPainterPath thumbPath;
    thumbPath.addRoundedRect(thumbRect, 6, 6);
    painter->fillPath(thumbPath, QColor(QStringLiteral("#f4f4f7")));
    painter->setPen(QPen(QColor(QStringLiteral("#e2e2e8")), 1));
    painter->drawPath(thumbPath);

    // 占位：窗口标题栏 + 两行内容，让人一眼看出"这里将来是画面"。
    const QRect fakeBar(thumbRect.left() + 8, thumbRect.top() + 8, thumbRect.width() - 16, 8);
    painter->fillRect(fakeBar, QColor(QStringLiteral("#e6e6ec")));
    painter->fillRect(QRect(fakeBar.left(), fakeBar.bottom() + 6, fakeBar.width(), 5),
                      QColor(QStringLiteral("#ededf2")));
    painter->fillRect(QRect(fakeBar.left(), fakeBar.bottom() + 15, fakeBar.width() * 2 / 3, 5),
                      QColor(QStringLiteral("#ededf2")));

    // ---- 文字：标题 / 进程名 / 失败原因（来自 item 的三行文本）----
    const QStringList lines = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n'));
    const int textLeft = checkRect.right() + 10;
    const int textWidth = thumbRect.left() - 10 - textLeft;

    QFont titleFont = option.font;
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(QColor(QStringLiteral("#1d1d1f")));
    const QFontMetrics titleMetrics(titleFont);
    painter->drawText(QRect(textLeft, row.top() + 12, textWidth, 20),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      elide(titleMetrics, lines.value(0), textWidth));

    painter->setFont(option.font);
    painter->setPen(QColor(QStringLiteral("#6e6e73")));
    const QFontMetrics metrics(option.font);
    painter->drawText(QRect(textLeft, row.top() + 32, textWidth, 18),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      elide(metrics, lines.value(1), textWidth));

    if (lines.size() > 2) {
        QString failure = lines.at(2);
        if (failure.startsWith(QStringLiteral("! "))) {
            failure = failure.mid(2);
        }
        painter->setPen(QColor(QStringLiteral("#d70015")));
        painter->drawText(QRect(textLeft, row.top() + 50, textWidth, 16),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          elide(metrics, failure, textWidth));
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
            const QRect row = option.rect.adjusted(0, 0, 0, -1);
            const QRect hit(row.left() + kPad - 6, row.center().y() - kCheckSize / 2 - 6,
                            kCheckSize + 12, kCheckSize + 12);
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
