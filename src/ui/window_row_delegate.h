#pragma once

#include <QStyledItemDelegate>

namespace crossdesk::ui {

// 左侧"任务栏"一行怎么画：勾选框 | 标题与进程名 | 右侧缩略图。
//
// 为什么不用 QListWidget 自带的图标：Qt 的图标永远画在文字左边，
// 而这里的语义是"任务的名字后面跟着它的画面"，所以只能自己画。
//
// 缩略图现在画的是占位（一个灰底窗口示意），等 A 的 DWM 接入后，
// 由 MainWindow 通过 setThumbnail() 换成真实画面，绘制代码不用改结构。
class WindowRowDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit WindowRowDelegate(QObject* parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;
};

}  // namespace crossdesk::ui
