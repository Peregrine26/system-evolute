#include <QCoreApplication>
#include "desktop/pages.h"
#include "desktop/shell.h"
#include "desktop/record_actions.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>

namespace crossdesk::desktop {
namespace {
using Notify = std::function<void(const QString&)>;
QPushButton* button(QBoxLayout* layout, const QString& text, const std::function<void()>& action) {
    auto* result = new QPushButton(text);
    layout->addWidget(result);
    QObject::connect(result, &QPushButton::clicked, result, action);
    return result;
}
QStringList selectedIds(QListWidget* list) {
    QStringList ids; for (auto* row : list->selectedItems()) ids << row->data(Qt::UserRole).toString(); return ids;
}
QIcon colorIcon(const QString& value) {
    if (!QColor(value).isValid()) return {};
    QPixmap pixmap(14, 14); pixmap.fill(Qt::transparent); QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen); painter.setBrush(QColor(value)); painter.drawEllipse(2, 2, 10, 10); return QIcon(pixmap);
}
class TaskDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem view(option); initStyleOption(&view, index);
        auto* style = view.widget ? view.widget->style() : QApplication::style();
        auto text = style->subElementRect(QStyle::SE_ItemViewItemText, &view, view.widget);
        const auto check = style->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &view, view.widget);
        const auto icon = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &view, view.widget);
        if (view.features.testFlag(QStyleOptionViewItem::HasCheckIndicator)) text.setLeft(qMax(text.left(), check.right() + 8));
        if (!view.icon.isNull()) text.setLeft(qMax(text.left(), icon.right() + 8));
        auto background = view; background.text.clear();
        painter->save(); style->drawControl(QStyle::CE_ItemViewItem, &background, painter, view.widget);
        painter->setFont(view.font);
        style->drawItemText(painter, text, Qt::AlignLeft | Qt::AlignVCenter, view.palette,
            view.state.testFlag(QStyle::State_Enabled), view.text,
            view.state.testFlag(QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text);
        painter->restore();
    }
};
class RecordPane final : public QWidget {
public:
    RecordPane(Store& store, QString key, QWidget* list, QWidget* detail)
        : store_(store), key_(std::move(key)), list_(list), detail_(detail) {
        auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
        toggle_ = button(layout, QCoreApplication::translate("CrossDesk", "查看详情"), [this] { showingDetail_ = !showingDetail_; arrange(); });
        toggle_->setObjectName(key_ + "PaneToggle"); splitter_ = new QSplitter(Qt::Horizontal);
        splitter_->setObjectName(key_ + "Splitter"); splitter_->addWidget(list); splitter_->addWidget(detail);
        splitter_->setStretchFactor(0, 1); splitter_->setStretchFactor(1, 2); layout->addWidget(splitter_, 1);
        const auto sizes = store_.settings()[key_ + "PaneSizes"].toArray();
        if (sizes.size() == 2) splitter_->setSizes({qMax(80, sizes[0].toInt()), qMax(80, sizes[1].toInt())});
        connect(splitter_, &QSplitter::splitterMoved, this, [this] {
            if (width() < 680 || focused_) return; auto settings = store_.settings(); QJsonArray sizes;
            for (int size : splitter_->sizes()) sizes.append(size); settings[key_ + "PaneSizes"] = sizes; store_.setSettings(settings);
        }); arrange();
    }
    void details() { showingDetail_ = true; arrange(); }
    void focus(bool value) { focused_ = value; arrange(); }
protected:
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); arrange(); }
private:
    void arrange() {
        const bool wide = width() >= 680; list_->setVisible(!focused_ && (wide || !showingDetail_));
        detail_->setVisible(focused_ || wide || showingDetail_); toggle_->setVisible(!focused_ && !wide);
        toggle_->setText(showingDetail_ ? QCoreApplication::translate("CrossDesk", "返回列表") : QCoreApplication::translate("CrossDesk", "查看详情"));
    }
    Store& store_; QString key_; QWidget* list_; QWidget* detail_; QPushButton* toggle_; QSplitter* splitter_; bool showingDetail_ = false, focused_ = false;
};
class AttachmentDialog final : public QDialog {
public:
    AttachmentDialog(Store& store, QString kind, QString id, QWidget* parent)
        : QDialog(parent), store_(store), kind_(std::move(kind)), id_(std::move(id)) {
        setObjectName("attachmentManager"); setWindowTitle(QCoreApplication::translate("CrossDesk", "附件管理")); resize(640, 420);
        auto* layout = new QVBoxLayout(this); auto* hint = new QLabel(QCoreApplication::translate("CrossDesk", "复制到数据目录或关联原文件。移除附件不会删除外部原文件。")); hint->setWordWrap(true); layout->addWidget(hint);
        list_ = new QListWidget; list_->setObjectName("attachmentList"); list_->setSelectionMode(QAbstractItemView::ExtendedSelection); layout->addWidget(list_, 1);
        auto* controls = new QHBoxLayout; mode_ = new QComboBox; mode_->setObjectName("attachmentMode"); mode_->addItems({QCoreApplication::translate("CrossDesk", "复制到数据目录"), QCoreApplication::translate("CrossDesk", "关联原文件")}); controls->addWidget(mode_);
        button(controls, QCoreApplication::translate("CrossDesk", "添加…"), [this] {
            const auto files = QFileDialog::getOpenFileNames(this, QCoreApplication::translate("CrossDesk", "添加附件")); if (files.isEmpty()) return;
            QString error; if (!addRecordAttachments(store_, kind_, id_, files, mode_->currentIndex() == 0, &error)) showError(error); refresh();
        })->setObjectName("addAttachments");
        button(controls, QCoreApplication::translate("CrossDesk", "打开"), [this] { for (const auto& path : paths()) QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
        button(controls, QCoreApplication::translate("CrossDesk", "复制 Markdown"), [this] {
            QStringList links; for (const auto& path : paths()) {
                auto name = QFileInfo(path).fileName(); name.replace('[', "\\[").replace(']', "\\]");
                const bool image = QStringList{"png", "jpg", "jpeg", "gif", "webp", "bmp"}.contains(QFileInfo(path).suffix().toLower());
                links << (image ? "!" : "") + QString("[") + name + "](" + QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded) + ')';
            } QApplication::clipboard()->setText(links.join('\n'));
        }); layout->addLayout(controls);
        auto* removal = new QHBoxLayout; deleteManaged_ = new QCheckBox(QCoreApplication::translate("CrossDesk", "同时删除无人引用的托管副本")); removal->addWidget(deleteManaged_, 1);
        button(removal, QCoreApplication::translate("CrossDesk", "移除所选"), [this] {
            const auto selected = paths(); if (selected.isEmpty()) return;
            if (deleteManaged_->isChecked() && QMessageBox::question(this, QCoreApplication::translate("CrossDesk", "移除附件"), QCoreApplication::translate("CrossDesk", "移除引用并删除无人引用的托管副本？外部原文件会保留。")) != QMessageBox::Yes) return;
            QString error; if (!removeRecordAttachments(store_, kind_, id_, selected, deleteManaged_->isChecked(), &error)) showError(error); refresh();
        })->setObjectName("removeAttachments"); layout->addLayout(removal);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(close); connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(list_, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* row) { QDesktopServices::openUrl(QUrl::fromLocalFile(row->data(Qt::UserRole).toString())); }); refresh();
    }
private:
    void showError(const QString& value) { if (!value.isEmpty()) QMessageBox::warning(this, QCoreApplication::translate("CrossDesk", "附件操作"), value); }
    QStringList paths() const { QStringList result; for (auto* row : list_->selectedItems()) result << row->data(Qt::UserRole).toString(); return result; }
    void refresh() {
        list_->clear(); for (const auto& path : recordAttachmentPaths(findRecord(store_, kind_, id_))) {
            auto* row = new QListWidgetItem(QFileInfo(path).fileName() + '\n' + path, list_); row->setData(Qt::UserRole, path); if (!QFileInfo::exists(path)) row->setForeground(QColor("#b35b35"));
        }
    }
    Store& store_; QString kind_, id_; QListWidget* list_; QComboBox* mode_; QCheckBox* deleteManaged_;
};
class TodoPage final : public QWidget {
public:
    TodoPage(Store& store, Notify notify) : store_(store), notify_(std::move(notify)) {
        auto* layout = new QVBoxLayout(this);
        auto* input = new QHBoxLayout;
        title_ = new QLineEdit; title_->setObjectName("todoTitle");
        title_->setPlaceholderText(QCoreApplication::translate("CrossDesk", "添加待办")); input->addWidget(title_);
        button(input, QCoreApplication::translate("CrossDesk", "添加"), [this] { add(); }); layout->addLayout(input);
        due_ = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1)); due_->setObjectName("todoDue"); due_->setCalendarPopup(true);
        due_->setDisplayFormat("yyyy-MM-dd HH:mm");
        recurrence_ = new QComboBox; recurrence_->setObjectName("todoRecurrence"); recurrence_->addItems({QCoreApplication::translate("CrossDesk", "不重复"), QCoreApplication::translate("CrossDesk", "每天"), QCoreApplication::translate("CrossDesk", "每周")});
        auto* schedule = new QHBoxLayout; schedule->addWidget(due_); schedule->addWidget(recurrence_);
        reminder_ = new QCheckBox(QCoreApplication::translate("CrossDesk", "提醒")); reminder_->setObjectName("todoReminder"); schedule->addWidget(reminder_); layout->addLayout(schedule);
        filter_ = new QComboBox; filter_->addItems({QCoreApplication::translate("CrossDesk", "全部"), QCoreApplication::translate("CrossDesk", "未完成"), QCoreApplication::translate("CrossDesk", "已完成"), QCoreApplication::translate("CrossDesk", "已过期")});
        filter_->setObjectName("todoFilter"); layout->addWidget(filter_);
        search_ = new QLineEdit; search_->setObjectName("todoTextFilter"); search_->setPlaceholderText(QCoreApplication::translate("CrossDesk", "筛选标题或备注")); layout->addWidget(search_);
        list_ = new QListWidget; list_->setObjectName("todoList"); list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        list_->setItemDelegate(new TaskDelegate(list_));
        detail_ = new QTextBrowser; detail_->setObjectName("todoMarkdownPreview"); detail_->setOpenExternalLinks(true);
        pane_ = new RecordPane(store_, "todo", list_, detail_); layout->addWidget(pane_, 1);
        auto* actions = new QHBoxLayout;
        button(actions, QCoreApplication::translate("CrossDesk", "编辑"), [this] { edit(false); })->setObjectName("editTodo");
        button(actions, QCoreApplication::translate("CrossDesk", "附件"), [this] { attach(); })->setObjectName("todoAttachments");
        button(actions, QCoreApplication::translate("CrossDesk", "明天"), [this] { edit(true); });
        button(actions, QCoreApplication::translate("CrossDesk", "转为随记"), [this] { convert(); });
        button(actions, QCoreApplication::translate("CrossDesk", "删除"), [this] { remove(); })->setObjectName("deleteTodo"); layout->addLayout(actions);
        auto* bulk = new QHBoxLayout;
        button(bulk, QCoreApplication::translate("CrossDesk", "完成所选"), [this] { completeTasks(store_, selectedIds(list_), true); })->setObjectName("completeSelectedTodos");
        button(bulk, QCoreApplication::translate("CrossDesk", "标为未完成"), [this] { completeTasks(store_, selectedIds(list_), false); })->setObjectName("uncompleteSelectedTodos");
        button(bulk, QCoreApplication::translate("CrossDesk", "颜色标记"), [this] { color(); })->setObjectName("colorSelectedTodos");
        button(bulk, QCoreApplication::translate("CrossDesk", "全选"), [this] { list_->selectAll(); }); layout->addLayout(bulk);
        connect(list_, &QListWidget::itemDoubleClicked, this, [this] { edit(false); });
        connect(title_, &QLineEdit::returnPressed, this, [this] { add(); });
        connect(filter_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
        connect(list_, &QListWidget::currentItemChanged, this, [this] { details(); });
        connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
            completeTasks(store_, {item->data(Qt::UserRole).toString()}, item->checkState() == Qt::Checked);
        });
        connect(&store_, &Store::changed, this, [this] { QTimer::singleShot(0, this, [this] { refresh(); }); });
        refresh();
    }
private:
    void add() {
        if (title_->text().trimmed().isEmpty()) return;
        auto records = store_.records("todos");
        records.append(QJsonObject{{"id", QUuid::createUuid().toString()}, {"title", title_->text().trimmed()},
            {"done", false}, {"due", due_->dateTime().toString(Qt::ISODate)}, {"repeat", recurrence_->currentIndex()},
            {"reminder", reminder_->isChecked()}, {"notes", ""}, {"attachments", QJsonArray{}}});
        if (store_.setRecords("todos", records)) title_->clear();
    }
    void refresh() {
        const auto selected = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString{};
        const auto selectedRows = selectedIds(list_);
        QSignalBlocker blocker(list_); list_->clear();
        for (const auto& value : store_.records("todos")) {
            const auto task = value.toObject(); const bool done = task["done"].toBool();
            const auto due = QDateTime::fromString(task["due"].toString(), Qt::ISODate);
            if ((filter_->currentIndex() == 1 && done) || (filter_->currentIndex() == 2 && !done) ||
                (filter_->currentIndex() == 3 && (done || !due.isValid() || due >= QDateTime::currentDateTime()))) continue;
            if (!(task["title"].toString() + '\n' + task["notes"].toString()).contains(search_->text(), Qt::CaseInsensitive)) continue;
            auto* row = new QListWidgetItem(colorIcon(task["color"].toString()), task["title"].toString() + "\n" + due.toString("MM-dd HH:mm"), list_);
            row->setData(Qt::UserRole, task["id"].toString()); row->setToolTip(task["notes"].toString());
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable); row->setCheckState(done ? Qt::Checked : Qt::Unchecked);
            if (!done && due < QDateTime::currentDateTime()) row->setForeground(QColor("#c05c35"));
            if (task["id"].toString() == selected) list_->setCurrentItem(row, QItemSelectionModel::NoUpdate);
            row->setSelected(selectedRows.contains(task["id"].toString()));
        }
        details();
    }
    void details() {
        const auto id = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString{};
        const auto task = findRecord(store_, "todos", id);
        if (task.isEmpty()) { detail_->setPlainText(QCoreApplication::translate("CrossDesk", "选择待办查看 Markdown 备注与附件。Ctrl / Shift 可多选。")); return; }
        QString text = "# " + task["title"].toString() + "\n\n" + (task["done"].toBool() ? QCoreApplication::translate("CrossDesk", "已完成") : QCoreApplication::translate("CrossDesk", "未完成")) + " · " + QDateTime::fromString(task["due"].toString(), Qt::ISODate).toString("yyyy-MM-dd HH:mm") + "\n\n" + task["notes"].toString();
        for (const auto& path : recordAttachmentPaths(task)) text += "\n\n[" + QFileInfo(path).fileName() + "](" + QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded) + ')';
        detail_->setMarkdown(text);
    }
    void color() {
        const auto ids = selectedIds(list_); if (ids.isEmpty()) return; QMenu menu;
        for (const auto& entry : {qMakePair(QCoreApplication::translate("CrossDesk", "无标记"), QString{}), qMakePair(QCoreApplication::translate("CrossDesk", "红色"), QString("#d35757")), qMakePair(QCoreApplication::translate("CrossDesk", "橙色"), QString("#c68b31")), qMakePair(QCoreApplication::translate("CrossDesk", "绿色"), QString("#43886c")), qMakePair(QCoreApplication::translate("CrossDesk", "蓝色"), QString("#397eb8")), qMakePair(QCoreApplication::translate("CrossDesk", "紫色"), QString("#8966bd"))}) {
            auto* action = menu.addAction(colorIcon(entry.second), entry.first); connect(action, &QAction::triggered, this, [this, ids, entry] {
                auto records = store_.records("todos"); for (int i = 0; i < records.size(); ++i) { auto task = records[i].toObject(); if (ids.contains(task["id"].toString())) { task["color"] = entry.second; records[i] = task; } } store_.setRecords("todos", records);
            });
        } menu.exec(QCursor::pos());
    }
    int currentIndex(const QJsonArray& records) const {
        if (!list_->currentItem()) return -1;
        const auto id = list_->currentItem()->data(Qt::UserRole).toString();
        for (int i = 0; i < records.size(); ++i) if (records[i].toObject()["id"].toString() == id) return i;
        return -1;
    }
    void edit(bool tomorrow) {
        auto records = store_.records("todos"); const int i = currentIndex(records); if (i < 0) return;
        auto task = records[i].toObject();
        if (tomorrow) task["due"] = QDateTime::currentDateTime().addDays(1).toString(Qt::ISODate);
        else {
            QDialog dialog(this); dialog.setObjectName("editTodoDialog"); dialog.setWindowTitle(QCoreApplication::translate("CrossDesk", "编辑待办"));
            auto* form = new QFormLayout(&dialog);
            auto* title = new QLineEdit(task["title"].toString()); title->setObjectName("editTodoTitle"); form->addRow(QCoreApplication::translate("CrossDesk", "任务"), title);
            auto* due = new QDateTimeEdit(QDateTime::fromString(task["due"].toString(), Qt::ISODate));
            due->setObjectName("editTodoDue"); due->setCalendarPopup(true); due->setDisplayFormat("yyyy-MM-dd HH:mm"); form->addRow(QCoreApplication::translate("CrossDesk", "截止时间"), due);
            auto* reminder = new QCheckBox(QCoreApplication::translate("CrossDesk", "到期提醒")); reminder->setObjectName("editTodoReminder"); reminder->setChecked(task["reminder"].toBool()); form->addRow(reminder);
            auto* repeat = new QComboBox; repeat->addItems({QCoreApplication::translate("CrossDesk", "不重复"), QCoreApplication::translate("CrossDesk", "每天"), QCoreApplication::translate("CrossDesk", "每周")});
            repeat->setCurrentIndex(std::clamp(task["repeat"].toInt(), 0, 2)); form->addRow(QCoreApplication::translate("CrossDesk", "重复"), repeat);
            auto* tabs = new QTabWidget; auto* notes = new QPlainTextEdit(task["notes"].toString()); notes->setObjectName("editTodoNotes");
            auto* preview = new QTextBrowser; preview->setOpenExternalLinks(true); preview->setMarkdown(notes->toPlainText());
            tabs->addTab(notes, QCoreApplication::translate("CrossDesk", "Markdown 备注")); tabs->addTab(preview, QCoreApplication::translate("CrossDesk", "预览")); form->addRow(tabs);
            connect(notes, &QPlainTextEdit::textChanged, preview, [notes, preview] { preview->setMarkdown(notes->toPlainText()); });
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); buttons->setObjectName("editTodoButtons"); form->addRow(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, [title, &dialog] { if (!title->text().trimmed().isEmpty()) dialog.accept(); });
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted) return;
            task["title"] = title->text().trimmed(); task["due"] = due->dateTime().toString(Qt::ISODate);
            task["reminder"] = reminder->isChecked(); task["repeat"] = repeat->currentIndex(); task["notes"] = notes->toPlainText();
        }
        // Resolve the stable ID again after the dialog's nested event loop.
        records = store_.records("todos");
        for (int current = 0; current < records.size(); ++current)
            if (records[current].toObject()["id"] == task["id"]) {
                auto latest = records[current].toObject();
                for (const auto* key : {"title", "due", "reminder", "repeat", "notes"}) latest[key] = task[key];
                records[current] = latest; store_.setRecords("todos", records); break;
            }
    }
    void remove() {
        auto ids = selectedIds(list_); if (ids.isEmpty()) return;
        if (ids.size() > 1 && QMessageBox::question(this, QCoreApplication::translate("CrossDesk", "删除待办"), QCoreApplication::translate("CrossDesk", "删除所选的 %1 条待办？附件原文件保留。").arg(ids.size())) != QMessageBox::Yes) return;
        removeRecords(store_, "todos", ids);
    }
    void convert() {
        auto records = store_.records("todos"); const int i = currentIndex(records); if (i < 0) return;
        store_.convertRecord("todos", "notes", records[i].toObject()["id"].toString());
    }
    void attach() {
        if (!list_->currentItem()) return; const auto id = list_->currentItem()->data(Qt::UserRole).toString();
        AttachmentDialog dialog(store_, "todos", id, this); dialog.exec();
    }
    Store& store_; Notify notify_; QLineEdit* title_; QDateTimeEdit* due_; QComboBox* recurrence_;
    QCheckBox* reminder_; QComboBox* filter_; QListWidget* list_;
    QLineEdit* search_; QTextBrowser* detail_; RecordPane* pane_;
};

class NotePage final : public QWidget {
public:
    explicit NotePage(Store& store) : store_(store) {
        auto* layout = new QVBoxLayout(this); auto* tools = new QHBoxLayout;
        button(tools, QCoreApplication::translate("CrossDesk", "新随记"), [this] {
            if (!flush()) return;
            auto records = store_.records("notes"); const auto id = QUuid::createUuid().toString();
            records.append(QJsonObject{{"id", id}, {"title", QCoreApplication::translate("CrossDesk", "新随记")}, {"body", ""}});
            if (!store_.setRecords("notes", records)) return;
            selected_ = id;
            { QSignalBlocker blocker(editor_); editor_->clear(); preview_->clear(); dirty_ = false; loadedBody_.clear(); }
            refresh(); pane_->details();
        })->setObjectName("newNote");
        button(tools, QCoreApplication::translate("CrossDesk", "转为待办"), [this] {
            if (!flush()) return; const auto note = current(); if (note.isEmpty()) return;
            if (store_.convertRecord("notes", "todos", selected_).isEmpty()) return;
            selected_.clear(); refresh();
        });
        button(tools, QCoreApplication::translate("CrossDesk", "附件"), [this] {
            if (!flush() || selected_.isEmpty()) return;
            AttachmentDialog dialog(store_, "notes", selected_, this); dialog.exec(); if (!dirty_) loadEditor();
        })->setObjectName("noteAttachments");
        button(tools, QCoreApplication::translate("CrossDesk", "导出 Markdown"), [this] {
            flush(); const auto file = QFileDialog::getSaveFileName(this, QCoreApplication::translate("CrossDesk", "导出随记"), {}, "Markdown (*.md)");
            if (file.isEmpty()) return; QSaveFile output(file);
            if (output.open(QIODevice::WriteOnly)) { output.write(editor_->toPlainText().toUtf8()); output.commit(); }
        });
        button(tools, QCoreApplication::translate("CrossDesk", "删除"), [this] {
            const auto ids = selectedIds(list_); if (ids.isEmpty()) return;
            if (ids.size() > 1 && QMessageBox::question(this, QCoreApplication::translate("CrossDesk", "删除随记"), QCoreApplication::translate("CrossDesk", "删除所选的 %1 篇随记？附件原文件保留。").arg(ids.size())) != QMessageBox::Yes) return;
            if (!flush()) return;
            if (removeRecords(store_, "notes", ids)) { if (ids.contains(selected_)) { selected_.clear(); dirty_ = false; } refresh(); }
        })->setObjectName("deleteNotes"); layout->addLayout(tools);
        auto* options = new QHBoxLayout;
        pin_ = new QCheckBox(QCoreApplication::translate("CrossDesk", "固定")); pin_->setObjectName("pinNote"); options->addWidget(pin_);
        paper_ = new QComboBox; paper_->setObjectName("notePaper"); paper_->addItems({QCoreApplication::translate("CrossDesk", "跟随主题"), QCoreApplication::translate("CrossDesk", "米色纸张"), QCoreApplication::translate("CrossDesk", "薄荷纸张"), QCoreApplication::translate("CrossDesk", "深色纸张")}); options->addWidget(paper_);
        auto* focus = new QCheckBox(QCoreApplication::translate("CrossDesk", "专注编辑")); focus->setObjectName("noteFocus"); options->addWidget(focus); options->addStretch(); layout->addLayout(options);
        list_ = new QListWidget; list_->setObjectName("noteList"); list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        auto* tabs = new QTabWidget; editor_ = new QPlainTextEdit; editor_->setObjectName("noteEditor");
        preview_ = new QTextBrowser; preview_->setObjectName("noteMarkdownPreview"); preview_->setOpenExternalLinks(true);
        tabs->addTab(editor_, QCoreApplication::translate("CrossDesk", "编辑")); tabs->addTab(preview_, QCoreApplication::translate("CrossDesk", "预览"));
        pane_ = new RecordPane(store_, "note", list_, tabs); layout->addWidget(pane_, 1);
        connect(pin_, &QCheckBox::toggled, this, [this](bool value) { if (!selected_.isEmpty()) patchRecord(store_, "notes", selected_, {{"pinned", value}}); });
        connect(paper_, &QComboBox::currentIndexChanged, this, [this](int index) { if (!selected_.isEmpty()) patchRecord(store_, "notes", selected_, {{"paper", index}}); applyPaper(); });
        connect(focus, &QCheckBox::toggled, this, [this, tools](bool enabled) {
            pane_->focus(enabled); for (int i = 0; i < tools->count(); ++i) if (auto* item = tools->itemAt(i)->widget()) item->setVisible(!enabled);
            if (enabled) { pane_->details(); editor_->setFocus(); }
        });
        saveTimer_.setSingleShot(true); saveTimer_.setInterval(400);
        connect(&saveTimer_, &QTimer::timeout, this, [this] { flush(); });
        connect(editor_, &QPlainTextEdit::textChanged, this, [this] { dirty_ = true; preview_->setMarkdown(editor_->toPlainText()); saveTimer_.start(); });
        connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* row) {
            if (!flush()) {
                QSignalBlocker blocker(list_);
                for (int i = 0; i < list_->count(); ++i) if (list_->item(i)->data(Qt::UserRole).toString() == selected_) { list_->setCurrentRow(i); break; }
                return;
            }
            selected_ = row ? row->data(Qt::UserRole).toString() : QString{};
            loadEditor();
        });
        connect(&store_, &Store::changed, this, [this] { QTimer::singleShot(0, this, [this] { refresh(); }); });
        refresh();
    }
    ~NotePage() override { flush(); }
    void flushNow() { flush(); }
private:
    void loadEditor() {
        QSignalBlocker block(editor_); loadedBody_ = current()["body"].toString();
        editor_->setPlainText(loadedBody_); preview_->setMarkdown(loadedBody_); dirty_ = false;
        QSignalBlocker pinBlock(pin_); QSignalBlocker paperBlock(paper_);
        pin_->setChecked(current()["pinned"].toBool()); paper_->setCurrentIndex(std::clamp(current()["paper"].toInt(), 0, 3)); applyPaper();
    }
    void applyPaper() {
        const QStringList backgrounds{"", "#fff7df", "#edf7ee", "#242c30"}; const int index = paper_->currentIndex();
        const auto sheet = index == 0 ? QString{} : QString("background-color:%1;color:%2;").arg(backgrounds[index], index == 3 ? "#e7eded" : "#283b35");
        editor_->setStyleSheet(sheet); preview_->setStyleSheet(sheet);
    }
    QJsonObject current() const {
        for (const auto& value : store_.records("notes")) if (value.toObject()["id"].toString() == selected_) return value.toObject();
        return {};
    }
    bool flush() {
        saveTimer_.stop(); if (selected_.isEmpty() || !dirty_) return true;
        auto records = store_.records("notes");
        for (int i = 0; i < records.size(); ++i) {
            auto note = records[i].toObject(); if (note["id"].toString() != selected_) continue;
            const auto text = editor_->toPlainText();
            if (note["body"].toString() == text) { dirty_ = false; loadedBody_ = text; return true; }
            if (note["body"].toString() != loadedBody_) {
                // Two note widgets edited the same record. Preserve both texts.
                const auto copyId = QUuid::createUuid().toString(); note["id"] = copyId;
                note["title"] = QCoreApplication::translate("CrossDesk", "并发编辑副本"); note["body"] = text;
                records.append(note);
                if (!store_.setRecords("notes", records)) return false;
                selected_ = copyId; dirty_ = false; loadedBody_ = text; return true;
            }
            note["body"] = text; note["title"] = text.section('\n', 0, 0).left(60).remove('#').trimmed();
            if (note["title"].toString().isEmpty()) note["title"] = QCoreApplication::translate("CrossDesk", "新随记");
            records[i] = note;
            if (!store_.setRecords("notes", records)) return false;
            dirty_ = false; loadedBody_ = text; return true;
        }
        const auto copyId = QUuid::createUuid().toString(); const auto text = editor_->toPlainText();
        records.append(QJsonObject{{"id", copyId}, {"title", QCoreApplication::translate("CrossDesk", "已删除记录的编辑副本")}, {"body", text}});
        if (!store_.setRecords("notes", records)) return false;
        selected_ = copyId; dirty_ = false; loadedBody_ = text; return true;
    }
    void refresh() {
        if (current().isEmpty()) selected_.clear();
        const auto selectedRows = selectedIds(list_);
        QSignalBlocker blocker(list_); list_->clear();
        QList<QJsonObject> notes; for (const auto& value : store_.records("notes")) notes << value.toObject();
        std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a["pinned"].toBool() && !b["pinned"].toBool(); });
        for (const auto& note : notes) {
            auto* row = new QListWidgetItem((note["pinned"].toBool() ? QStringLiteral("★ ") : QString{}) + note["title"].toString(), list_);
            row->setData(Qt::UserRole, note["id"].toString());
            if (note["id"].toString() == selected_) list_->setCurrentItem(row, QItemSelectionModel::NoUpdate);
            row->setSelected(selectedRows.contains(note["id"].toString()) || note["id"].toString() == selected_);
        }
        if (selected_.isEmpty() && list_->count()) {
            selected_ = list_->item(0)->data(Qt::UserRole).toString(); list_->setCurrentRow(0);
            loadEditor();
        }
        if (!dirty_ && !selected_.isEmpty() && current()["body"].toString() != loadedBody_) loadEditor();
        { QSignalBlocker pinBlock(pin_); pin_->setChecked(current()["pinned"].toBool()); }
        { QSignalBlocker paperBlock(paper_); paper_->setCurrentIndex(std::clamp(current()["paper"].toInt(), 0, 3)); applyPaper(); }
        editor_->setEnabled(!selected_.isEmpty());
        if (list_->count() == 0) { QSignalBlocker editBlock(editor_); editor_->clear(); preview_->clear(); dirty_ = false; loadedBody_.clear(); }
    }
    Store& store_; QListWidget* list_; QPlainTextEdit* editor_; QTextBrowser* preview_; QString selected_; QTimer saveTimer_;
    bool dirty_ = false; QString loadedBody_;
    QCheckBox* pin_; QComboBox* paper_; RecordPane* pane_;
};


}
QWidget* createRecordPage(Store& store, const QString& kind, const std::function<void(const QString&)>& notify) { return kind == "todo" ? static_cast<QWidget*>(new TodoPage(store, notify)) : new NotePage(store); }
void flushRecordPages(QWidget* root) {
    if (auto* page = dynamic_cast<NotePage*>(root)) page->flushNow();
    for (auto* child : root->findChildren<QWidget*>()) if (auto* page = dynamic_cast<NotePage*>(child)) page->flushNow();
}
}
