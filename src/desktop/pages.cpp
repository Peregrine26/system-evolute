#include "desktop/pages.h"
#include "desktop/shell.h"

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
class DropModel final : public QFileSystemModel {
public:
    DropModel(Store& store, Notify notify, QObject* parent)
        : QFileSystemModel(parent), store_(store), notify_(std::move(notify)) { setReadOnly(false); }
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int, int,
                      const QModelIndex& parent) override {
        if (action == Qt::IgnoreAction) return true;
        if (!data->hasUrls()) return false;
        QString directory = filePath(parent);
        if (!QFileInfo(directory).isDir()) directory = QFileInfo(directory).absolutePath();
        int failures = 0;
        for (const auto& url : data->urls()) {
            if (!url.isLocalFile()) { ++failures; continue; }
            const auto source = url.toLocalFile();
            const auto target = directory + '/' + QFileInfo(source).fileName();
            const auto result = action == Qt::MoveAction ? movePath(source, target) : copyPath(source, target);
            if (!result.ok) { ++failures; notify_(result.error); }
            else if (action == Qt::MoveAction) recordMove(store_, source, target);
        }
        if (!failures) notify_(QStringLiteral("文件操作完成"));
        return failures == 0;
    }
private:
    Store& store_;
    Notify notify_;
};

class FolderPage final : public QWidget {
public:
    FolderPage(Store& store, QString id, Notify notify)
        : store_(store), id_(std::move(id)), notify_(std::move(notify)) {
        auto* layout = new QVBoxLayout(this);
        auto* tools = new QHBoxLayout;
        path_ = new QLineEdit(store_.widget(id_)["data"].toObject()["path"].toString());
        path_->setObjectName("folderPath");
        path_->setPlaceholderText(QStringLiteral("选择要映射的文件夹"));
        layout->addWidget(path_);
        layout->addLayout(tools);
        button(tools, QStringLiteral("选择"), [this] {
            const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("映射文件夹"), path_->text());
            if (!path.isEmpty()) navigate(path, true);
        });
        button(tools, QStringLiteral("上级"), [this] { navigate(QFileInfo(path_->text()).absolutePath(), false); });
        button(tools, QStringLiteral("新建"), [this] {
            const auto name = QInputDialog::getText(this, QStringLiteral("新建文件夹"), QStringLiteral("名称"));
            if (name.isEmpty()) return;
            if (name.contains('/') || name.contains('\\') || name == "." || name == ".." ||
                !QDir(path_->text()).mkdir(name)) notify_(QStringLiteral("无法新建文件夹"));
        });
        auto* mode = new QComboBox;
        mode->addItems({QStringLiteral("图标"), QStringLiteral("列表")}); tools->addWidget(mode);
        auto* sort = new QComboBox;
        sort->addItems({QStringLiteral("名称"), QStringLiteral("大小"), QStringLiteral("类型"), QStringLiteral("日期")});
        tools->addWidget(sort);
        view_ = new QListView;
        view_->setObjectName("folderView");
        model_ = new DropModel(store_, notify_, this);
        model_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        view_->setModel(model_);
        view_->setViewMode(QListView::IconMode);
        view_->setResizeMode(QListView::Adjust);
        view_->setIconSize(QSize(48, 48));
        view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view_->setDragDropMode(QAbstractItemView::DragDrop);
        view_->setDefaultDropAction(Qt::CopyAction);
        view_->setContextMenuPolicy(Qt::CustomContextMenu);
        layout->addWidget(view_, 1);
        connect(mode, &QComboBox::currentIndexChanged, this, [this](int index) {
            view_->setViewMode(index ? QListView::ListMode : QListView::IconMode);
        });
        connect(sort, &QComboBox::currentIndexChanged, model_, [this](int index) { model_->sort(index); });
        connect(path_, &QLineEdit::returnPressed, this, [this] { navigate(path_->text(), true); });
        connect(view_, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
            const auto path = model_->filePath(index);
            if (QFileInfo(path).isDir()) navigate(path, false);
            else QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        connect(view_, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) { context(point); });
        navigate(path_->text(), false);
    }
private:
    void navigate(const QString& path, bool persist) {
        if (!QFileInfo(path).isDir()) { notify_(QStringLiteral("文件夹不存在，请重新选择")); return; }
        path_->setText(QFileInfo(path).absoluteFilePath());
        view_->setRootIndex(model_->setRootPath(path_->text()));
        if (persist) {
            auto widget = store_.widget(id_);
            auto data = widget["data"].toObject(); data["path"] = path_->text(); widget["data"] = data;
            store_.putWidget(widget);
        }
    }
    QStringList selected() const {
        QStringList files;
        for (const auto& index : view_->selectionModel()->selectedRows()) files << model_->filePath(index);
        return files;
    }
    void transfer(bool move) {
        const auto* mime = QApplication::clipboard()->mimeData();
        if (!mime->hasUrls()) return;
        for (const auto& url : mime->urls()) {
            if (!url.isLocalFile()) continue;
            const auto source = url.toLocalFile();
            const auto target = path_->text() + '/' + QFileInfo(source).fileName();
            const auto result = move ? movePath(source, target) : copyPath(source, target);
            if (!result.ok) notify_(result.error);
            else if (move) recordMove(store_, source, target);
        }
    }
    void context(const QPoint& point) {
        QMenu menu;
        const auto files = selected();
        if (!files.isEmpty()) {
            menu.addAction(QStringLiteral("打开"), this, [files] { for (const auto& file : files) QDesktopServices::openUrl(QUrl::fromLocalFile(file)); });
            menu.addAction(QStringLiteral("复制"), this, [files] {
                auto* mime = new QMimeData; QList<QUrl> urls;
                for (const auto& file : files) urls << QUrl::fromLocalFile(file);
                mime->setUrls(urls); QApplication::clipboard()->setMimeData(mime);
            });
            menu.addAction(QStringLiteral("重命名"), this, [this] { view_->edit(view_->currentIndex()); });
            menu.addAction(QStringLiteral("移至…"), this, [this, files] {
                const auto target = QFileDialog::getExistingDirectory(this, QStringLiteral("移动到文件夹"));
                if (target.isEmpty()) return;
                for (const auto& file : files) {
                    const auto result = movePath(file, target + '/' + QFileInfo(file).fileName());
                    if (result.ok) recordMove(store_, file, result.destination); else notify_(result.error);
                }
            });
            menu.addAction(QStringLiteral("移到回收站"), this, [this, files] {
                if (QMessageBox::question(this, QStringLiteral("移到回收站"),
                    QStringLiteral("将选中的 %1 个项目移到回收站？").arg(files.size())) != QMessageBox::Yes) return;
                for (const auto& file : files) if (!QFile::moveToTrash(file)) notify_(QStringLiteral("无法移到回收站：") + file);
            });
            menu.addSeparator();
            menu.addAction(QStringLiteral("打开方式"), this, [this, files] { if (!openWith(files.first(), winId())) notify_(QStringLiteral("当前文件不支持打开方式")); });
            menu.addAction(QStringLiteral("属性"), this, [this, files] { if (!fileProperties(files.first(), winId())) notify_(QStringLiteral("无法显示文件属性")); });
            menu.addAction(QStringLiteral("系统右键菜单"), this, [this, files] {
                if (!systemFileMenu(files.first(), winId(), QCursor::pos())) notify_(QStringLiteral("系统菜单不可用"));
            });
        }
        menu.addAction(QStringLiteral("粘贴副本"), this, [this] { transfer(false); });
        menu.addAction(QStringLiteral("导入文件…"), this, [this] {
            for (const auto& file : QFileDialog::getOpenFileNames(this, QStringLiteral("导入文件"))) {
                const auto result = copyPath(file, path_->text() + '/' + QFileInfo(file).fileName());
                if (!result.ok) notify_(result.error);
            }
        });
        menu.addAction(QStringLiteral("在资源管理器中打开"), this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(path_->text())); });
        menu.addAction(QStringLiteral("整理当前文件夹…"), this, [this] { organizeFolder(store_, path_->text(), this, notify_); });
        menu.exec(view_->viewport()->mapToGlobal(point));
    }
    Store& store_; QString id_; Notify notify_;
    QLineEdit* path_; QListView* view_; DropModel* model_;
};

class TodoPage final : public QWidget {
public:
    TodoPage(Store& store, Notify notify) : store_(store), notify_(std::move(notify)) {
        auto* layout = new QVBoxLayout(this);
        auto* input = new QHBoxLayout;
        title_ = new QLineEdit; title_->setObjectName("todoTitle");
        title_->setPlaceholderText(QStringLiteral("添加待办")); input->addWidget(title_);
        button(input, QStringLiteral("添加"), [this] { add(); }); layout->addLayout(input);
        due_ = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1)); due_->setObjectName("todoDue"); due_->setCalendarPopup(true);
        due_->setDisplayFormat("yyyy-MM-dd HH:mm");
        recurrence_ = new QComboBox; recurrence_->setObjectName("todoRecurrence"); recurrence_->addItems({QStringLiteral("不重复"), QStringLiteral("每天"), QStringLiteral("每周")});
        auto* schedule = new QHBoxLayout; schedule->addWidget(due_); schedule->addWidget(recurrence_);
        reminder_ = new QCheckBox(QStringLiteral("提醒")); reminder_->setObjectName("todoReminder"); schedule->addWidget(reminder_); layout->addLayout(schedule);
        filter_ = new QComboBox; filter_->addItems({QStringLiteral("全部"), QStringLiteral("未完成"), QStringLiteral("已完成"), QStringLiteral("已过期")});
        layout->addWidget(filter_);
        list_ = new QListWidget; list_->setObjectName("todoList"); layout->addWidget(list_, 1);
        auto* actions = new QHBoxLayout;
        button(actions, QStringLiteral("编辑"), [this] { edit(false); })->setObjectName("editTodo");
        button(actions, QStringLiteral("附件"), [this] { attach(); });
        button(actions, QStringLiteral("明天"), [this] { edit(true); });
        button(actions, QStringLiteral("转为随记"), [this] { convert(); });
        button(actions, QStringLiteral("删除"), [this] { remove(); })->setObjectName("deleteTodo"); layout->addLayout(actions);
        connect(list_, &QListWidget::itemDoubleClicked, this, [this] { edit(false); });
        connect(title_, &QLineEdit::returnPressed, this, [this] { add(); });
        connect(filter_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
            auto records = store_.records("todos");
            for (int i = 0; i < records.size(); ++i) {
                auto task = records[i].toObject();
                if (task["id"].toString() != item->data(Qt::UserRole).toString()) continue;
                const bool done = item->checkState() == Qt::Checked;
                task["done"] = done; records[i] = task;
                if (done && task["repeat"].toInt() > 0 && !task["nextCreated"].toBool()) {
                    task["nextCreated"] = true; records[i] = task;
                    auto next = task; next["id"] = QUuid::createUuid().toString(); next["done"] = false;
                    next["nextCreated"] = false;
                    auto date = QDateTime::fromString(task["due"].toString(), Qt::ISODate);
                    const int days = task["repeat"].toInt() == 1 ? 1 : 7;
                    do { date = date.addDays(days); } while (date.isValid() && date < QDateTime::currentDateTime());
                    next["due"] = date.toString(Qt::ISODate); records.append(next);
                }
                store_.setRecords("todos", records); break;
            }
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
        QSignalBlocker blocker(list_); list_->clear();
        for (const auto& value : store_.records("todos")) {
            const auto task = value.toObject(); const bool done = task["done"].toBool();
            const auto due = QDateTime::fromString(task["due"].toString(), Qt::ISODate);
            if ((filter_->currentIndex() == 1 && done) || (filter_->currentIndex() == 2 && !done) ||
                (filter_->currentIndex() == 3 && (done || due >= QDateTime::currentDateTime()))) continue;
            auto* row = new QListWidgetItem(task["title"].toString() + "\n" + due.toString("MM-dd HH:mm"), list_);
            row->setData(Qt::UserRole, task["id"].toString()); row->setToolTip(task["notes"].toString());
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable); row->setCheckState(done ? Qt::Checked : Qt::Unchecked);
            if (!done && due < QDateTime::currentDateTime()) row->setForeground(QColor("#c05c35"));
            if (task["id"].toString() == selected) list_->setCurrentItem(row);
        }
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
            QDialog dialog(this); dialog.setObjectName("editTodoDialog"); dialog.setWindowTitle(QStringLiteral("编辑待办"));
            auto* form = new QFormLayout(&dialog);
            auto* title = new QLineEdit(task["title"].toString()); title->setObjectName("editTodoTitle"); form->addRow(QStringLiteral("任务"), title);
            auto* due = new QDateTimeEdit(QDateTime::fromString(task["due"].toString(), Qt::ISODate));
            due->setObjectName("editTodoDue"); due->setCalendarPopup(true); due->setDisplayFormat("yyyy-MM-dd HH:mm"); form->addRow(QStringLiteral("截止时间"), due);
            auto* reminder = new QCheckBox(QStringLiteral("到期提醒")); reminder->setObjectName("editTodoReminder"); reminder->setChecked(task["reminder"].toBool()); form->addRow(reminder);
            auto* repeat = new QComboBox; repeat->addItems({QStringLiteral("不重复"), QStringLiteral("每天"), QStringLiteral("每周")});
            repeat->setCurrentIndex(std::clamp(task["repeat"].toInt(), 0, 2)); form->addRow(QStringLiteral("重复"), repeat);
            auto* notes = new QPlainTextEdit(task["notes"].toString()); form->addRow(QStringLiteral("备注"), notes);
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
        auto records = store_.records("todos"); const int i = currentIndex(records); if (i < 0) return;
        records.removeAt(i); store_.setRecords("todos", records);
    }
    void convert() {
        auto records = store_.records("todos"); const int i = currentIndex(records); if (i < 0) return;
        store_.convertRecord("todos", "notes", records[i].toObject()["id"].toString());
    }
    void attach() {
        auto records = store_.records("todos"); const int i = currentIndex(records); if (i < 0) return;
        auto task = records[i].toObject(); auto attachments = task["attachments"].toArray();
        const auto files = QFileDialog::getOpenFileNames(this, QStringLiteral("添加附件"));
        for (const auto& file : files) {
            const auto directory = store_.directory() + "/attachments/" + task["id"].toString(); QDir().mkpath(directory);
            const auto destination = directory + '/' + QFileInfo(file).fileName();
            const auto result = copyPath(file, destination);
            if (result.ok) attachments.append(destination); else notify_(result.error);
        }
        task["attachments"] = attachments; records[i] = task; store_.setRecords("todos", records);
        if (files.isEmpty() && !attachments.isEmpty()) {
            QStringList paths; for (const auto& value : attachments) paths << value.toString();
            const auto selected = QInputDialog::getItem(this, QStringLiteral("打开附件"), QStringLiteral("附件"), paths, 0, false);
            if (!selected.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(selected));
        }
    }
    Store& store_; Notify notify_; QLineEdit* title_; QDateTimeEdit* due_; QComboBox* recurrence_;
    QCheckBox* reminder_; QComboBox* filter_; QListWidget* list_;
};

class NotePage final : public QWidget {
public:
    explicit NotePage(Store& store) : store_(store) {
        auto* layout = new QVBoxLayout(this); auto* tools = new QHBoxLayout;
        button(tools, QStringLiteral("新随记"), [this] {
            flush(); auto records = store_.records("notes"); selected_ = QUuid::createUuid().toString();
            records.append(QJsonObject{{"id", selected_}, {"title", QStringLiteral("新随记")}, {"body", ""}});
            store_.setRecords("notes", records);
            { QSignalBlocker blocker(editor_); editor_->clear(); preview_->clear(); dirty_ = false; loadedBody_.clear(); }
            refresh();
        })->setObjectName("newNote");
        button(tools, QStringLiteral("转为待办"), [this] {
            flush(); const auto note = current(); if (note.isEmpty()) return;
            store_.convertRecord("notes", "todos", selected_); selected_.clear(); refresh();
        });
        button(tools, QStringLiteral("附件"), [this] {
            if (selected_.isEmpty()) return;
            const auto file = QFileDialog::getOpenFileName(this, QStringLiteral("添加随记附件")); if (file.isEmpty()) return;
            const auto directory = store_.directory() + "/attachments/" + selected_; QDir().mkpath(directory);
            const auto target = directory + '/' + QFileInfo(file).fileName(); const auto result = copyPath(file, target);
            if (!result.ok) { QMessageBox::warning(this, QStringLiteral("附件未添加"), result.error); return; }
            const auto extension = QFileInfo(file).suffix().toLower();
            const bool image = QStringList{"png", "jpg", "jpeg", "gif", "webp", "bmp"}.contains(extension);
            auto name = QFileInfo(file).fileName(); name.replace('[', "\\[").replace(']', "\\]");
            editor_->insertPlainText('\n' + (image ? QString("!") : QString{}) + '[' + name + "](" + QUrl::fromLocalFile(target).toString() + ")\n");
        });
        button(tools, QStringLiteral("导出 Markdown"), [this] {
            flush(); const auto file = QFileDialog::getSaveFileName(this, QStringLiteral("导出随记"), {}, "Markdown (*.md)");
            if (file.isEmpty()) return; QSaveFile output(file);
            if (output.open(QIODevice::WriteOnly)) { output.write(editor_->toPlainText().toUtf8()); output.commit(); }
        });
        button(tools, QStringLiteral("删除"), [this] {
            saveTimer_.stop(); auto records = store_.records("notes");
            for (int i = 0; i < records.size(); ++i) if (records[i].toObject()["id"].toString() == selected_) { records.removeAt(i); break; }
            selected_.clear(); store_.setRecords("notes", records); refresh();
        }); layout->addLayout(tools);
        auto* splitter = new QSplitter(Qt::Vertical); list_ = new QListWidget; splitter->addWidget(list_);
        auto* tabs = new QTabWidget; editor_ = new QPlainTextEdit; editor_->setObjectName("noteEditor");
        preview_ = new QTextBrowser; preview_->setOpenExternalLinks(true);
        tabs->addTab(editor_, QStringLiteral("编辑")); tabs->addTab(preview_, QStringLiteral("预览")); splitter->addWidget(tabs);
        splitter->setStretchFactor(1, 2); layout->addWidget(splitter);
        saveTimer_.setSingleShot(true); saveTimer_.setInterval(400);
        connect(&saveTimer_, &QTimer::timeout, this, [this] { flush(); });
        connect(editor_, &QPlainTextEdit::textChanged, this, [this] { dirty_ = true; preview_->setMarkdown(editor_->toPlainText()); saveTimer_.start(); });
        connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* row) {
            flush(); selected_ = row ? row->data(Qt::UserRole).toString() : QString{};
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
    }
    QJsonObject current() const {
        for (const auto& value : store_.records("notes")) if (value.toObject()["id"].toString() == selected_) return value.toObject();
        return {};
    }
    void flush() {
        saveTimer_.stop(); if (selected_.isEmpty() || !dirty_) return;
        auto records = store_.records("notes");
        for (int i = 0; i < records.size(); ++i) {
            auto note = records[i].toObject(); if (note["id"].toString() != selected_) continue;
            const auto text = editor_->toPlainText();
            if (note["body"].toString() == text) { dirty_ = false; loadedBody_ = text; return; }
            if (note["body"].toString() != loadedBody_) {
                // Two note widgets edited the same record. Preserve both texts.
                selected_ = QUuid::createUuid().toString(); note["id"] = selected_;
                note["title"] = QStringLiteral("并发编辑副本"); note["body"] = text;
                records.append(note);
                if (store_.setRecords("notes", records)) { dirty_ = false; loadedBody_ = text; }
                return;
            }
            note["body"] = text; note["title"] = text.section('\n', 0, 0).left(60).remove('#').trimmed();
            if (note["title"].toString().isEmpty()) note["title"] = QStringLiteral("新随记");
            records[i] = note;
            if (store_.setRecords("notes", records)) { dirty_ = false; loadedBody_ = text; }
            return;
        }
    }
    void refresh() {
        if (current().isEmpty()) selected_.clear();
        QSignalBlocker blocker(list_); list_->clear();
        for (const auto& value : store_.records("notes")) {
            const auto note = value.toObject(); auto* row = new QListWidgetItem(note["title"].toString(), list_);
            row->setData(Qt::UserRole, note["id"].toString());
            if (note["id"].toString() == selected_) list_->setCurrentItem(row);
        }
        if (selected_.isEmpty() && list_->count()) {
            selected_ = list_->item(0)->data(Qt::UserRole).toString(); list_->setCurrentRow(0);
            loadEditor();
        }
        if (!dirty_ && !selected_.isEmpty() && current()["body"].toString() != loadedBody_) loadEditor();
        editor_->setEnabled(!selected_.isEmpty());
        if (list_->count() == 0) { QSignalBlocker editBlock(editor_); editor_->clear(); preview_->clear(); dirty_ = false; loadedBody_.clear(); }
    }
    Store& store_; QListWidget* list_; QPlainTextEdit* editor_; QTextBrowser* preview_; QString selected_; QTimer saveTimer_;
    bool dirty_ = false; QString loadedBody_;
};

class SearchPage final : public QWidget {
public:
    SearchPage(Store& store, Notify notify) : store_(store), notify_(std::move(notify)) {
        auto* layout = new QVBoxLayout(this); query_ = new QLineEdit;
        query_->setPlaceholderText(QStringLiteral("搜索文件、待办、随记")); query_->setObjectName("searchQuery");
        layout->addWidget(query_); status_ = new QLabel; status_->setWordWrap(true); layout->addWidget(status_);
        results_ = new QListWidget; layout->addWidget(results_, 1);
        debounce_.setSingleShot(true); debounce_.setInterval(300);
        connect(query_, &QLineEdit::textChanged, this, [this] { debounce_.start(); });
        connect(&debounce_, &QTimer::timeout, this, [this] { search(); });
        connect(results_, &QListWidget::itemActivated, this, [](QListWidgetItem* item) {
            const auto path = item->data(Qt::UserRole).toString(); if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
    }
private:
    void search() {
        const auto query = query_->text().trimmed(); const auto generation = ++generation_;
        results_->clear(); if (query.isEmpty()) return;
        QStringList roots;
        for (const auto& item : store_.widgets()) if (item.toObject()["kind"] == "folder")
            roots << item.toObject()["data"].toObject()["path"].toString();
        for (const auto* key : {"todos", "notes"}) for (const auto& value : store_.records(key)) {
            const auto record = value.toObject(); const auto text = record["title"].toString() + '\n' + record["body"].toString() + record["notes"].toString();
            if (text.contains(query, Qt::CaseInsensitive)) {
                auto* row = new QListWidgetItem((QString(key) == "todos" ? QStringLiteral("待办 · ") : QStringLiteral("随记 · ")) + record["title"].toString(), results_);
                row->setToolTip(text);
            }
        }
        status_->setText(QStringLiteral("正在搜索…"));
        auto* watcher = new QFutureWatcher<QPair<QStringList, QString>>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation] {
            const auto result = watcher->result(); watcher->deleteLater(); if (generation != generation_) return;
            for (const auto& path : result.first) {
                auto* row = new QListWidgetItem(QFileInfo(path).fileName() + '\n' + path, results_);
                row->setData(Qt::UserRole, path);
            }
            status_->setText(result.second.isEmpty() ? QStringLiteral("%1 项结果").arg(results_->count()) : result.second);
        });
        watcher->setFuture(QtConcurrent::run([roots, query] {
            QString message; auto paths = everythingSearch(query, &message);
            if (!message.isEmpty()) for (const auto& root : roots) {
                QDirIterator files(root, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                int visited = 0;
                while (files.hasNext() && visited++ < 50000 && paths.size() < 500) {
                    const auto path = files.next(); if (files.fileName().contains(query, Qt::CaseInsensitive)) paths << path;
                }
            }
            paths.removeDuplicates(); return qMakePair(paths, message);
        }));
    }
    Store& store_; Notify notify_; QLineEdit* query_; QLabel* status_; QListWidget* results_; QTimer debounce_; quint64 generation_ = 0;
};

class WeatherPage final : public QWidget {
public:
    WeatherPage(Store& store, QString id) : store_(store), id_(std::move(id)) {
        auto* layout = new QVBoxLayout(this); city_ = new QLineEdit; city_->setPlaceholderText(QStringLiteral("城市名称"));
        auto data = store_.widget(id_)["data"].toObject(); city_->setText(data["city"].toString(QStringLiteral("Shanghai")));
        layout->addWidget(city_); weather_ = new QLabel(QStringLiteral("输入城市，获取天气")); weather_->setWordWrap(true); layout->addWidget(weather_, 1);
        button(layout, QStringLiteral("刷新天气"), [this] { locate(); });
        connect(city_, &QLineEdit::returnPressed, this, [this] { locate(); });
        auto* refresh = new QTimer(this); refresh->setInterval(30 * 60 * 1000);
        connect(refresh, &QTimer::timeout, this, [this] { locate(); }); refresh->start();
        if (!qEnvironmentVariableIsSet("CROSSDESK_OFFLINE")) QTimer::singleShot(0, this, [this] { locate(); });
    }
private:
    void locate() {
        if (qEnvironmentVariableIsSet("CROSSDESK_OFFLINE")) { weather_->setText(QStringLiteral("离线模式")); return; }
        const auto city = city_->text().trimmed(); if (city.isEmpty()) return; const auto requestId = ++request_;
        weather_->setText(QStringLiteral("正在获取天气…"));
        QUrl url("https://geocoding-api.open-meteo.com/v1/search"); QUrlQuery query;
        query.addQueryItem("name", city); query.addQueryItem("count", "1"); query.addQueryItem("language", "zh"); url.setQuery(query);
        QNetworkRequest request(url); request.setTransferTimeout(12000);
        auto* reply = network_.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, city, requestId] {
            const auto bytes = reply->readAll(); const auto error = reply->error(); reply->deleteLater(); if (requestId != request_) return;
            const auto results = QJsonDocument::fromJson(bytes).object()["results"].toArray();
            if (error != QNetworkReply::NoError || results.isEmpty()) { weather_->setText(QStringLiteral("找不到城市或网络不可用，点击刷新重试")); return; }
            const auto location = results.first().toObject();
            auto widget = store_.widget(id_); auto data = widget["data"].toObject(); data["city"] = city; widget["data"] = data; store_.putWidget(widget);
            QUrl forecast("https://api.open-meteo.com/v1/forecast"); QUrlQuery params;
            params.addQueryItem("latitude", QString::number(location["latitude"].toDouble()));
            params.addQueryItem("longitude", QString::number(location["longitude"].toDouble()));
            params.addQueryItem("current", "temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m");
            params.addQueryItem("daily", "temperature_2m_max,temperature_2m_min"); params.addQueryItem("timezone", "auto");
            forecast.setQuery(params); QNetworkRequest request(forecast); request.setTransferTimeout(12000);
            auto* response = network_.get(request);
            connect(response, &QNetworkReply::finished, this, [this, response, city, requestId] {
                const auto bytes = response->readAll(); const auto error = response->error(); response->deleteLater(); if (requestId != request_) return;
                const auto data = QJsonDocument::fromJson(bytes).object(); const auto current = data["current"].toObject();
                if (error != QNetworkReply::NoError || !current.contains("temperature_2m")) { weather_->setText(QStringLiteral("天气服务暂不可用")); return; }
                QString text = QStringLiteral("%1\n%2 °C\n湿度 %3% · 风速 %4 km/h\n\n未来 7 天\n").arg(city)
                    .arg(current["temperature_2m"].toDouble()).arg(current["relative_humidity_2m"].toDouble()).arg(current["wind_speed_10m"].toDouble());
                const auto daily = data["daily"].toObject(); const auto dates = daily["time"].toArray();
                const auto highs = daily["temperature_2m_max"].toArray(); const auto lows = daily["temperature_2m_min"].toArray();
                for (int i = 0; i < dates.size() && i < highs.size() && i < lows.size(); ++i)
                    text += QStringLiteral("%1   %2° / %3°\n").arg(dates[i].toString().mid(5)).arg(lows[i].toDouble()).arg(highs[i].toDouble());
                weather_->setText(text);
            });
        });
    }
    Store& store_; QString id_; QLineEdit* city_; QLabel* weather_; QNetworkAccessManager network_; quint64 request_ = 0;
};
}

void organizeFolder(Store& store, const QString& folder, QWidget* parent, const Notify& notify) {
    const auto files = QDir(folder).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    if (!QFileInfo(folder).isDir() || files.isEmpty()) { notify(QStringLiteral("没有可整理的文件")); return; }
    QDialog dialog(parent); dialog.setObjectName("organizationPreview"); dialog.setWindowTitle(QStringLiteral("整理预览")); dialog.resize(720, 440);
    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(files.size(), 2); table->setHorizontalHeaderLabels({QStringLiteral("原文件"), QStringLiteral("目标目录")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); table->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table);
    const QStringList categories{QStringLiteral("图片"), QStringLiteral("文档"), QStringLiteral("影音"), QStringLiteral("压缩包"), QStringLiteral("快捷方式"), QStringLiteral("其他")};
    for (int i = 0; i < files.size(); ++i) {
        auto* name = new QTableWidgetItem(files[i].fileName()); name->setCheckState(Qt::Checked); table->setItem(i, 0, name);
        auto* target = new QComboBox;
        for (const auto& category : categories) target->addItem(category, folder + '/' + category);
        target->setCurrentText(categoryFor(files[i].absoluteFilePath()));
        for (const auto& value : store.widgets()) {
            const auto widget = value.toObject(); if (widget["kind"].toString() != "folder") continue;
            const auto path = widget["data"].toObject()["path"].toString();
            target->addItem(QStringLiteral("现有格子 · ") + widget["title"].toString(), path);
        }
        table->setCellWidget(i, 1, target);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel); buttons->setObjectName("organizationButtons"); layout->addWidget(buttons);
    QObject::connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    int success = 0; QStringList errors;
    for (int i = 0; i < files.size(); ++i) {
        if (table->item(i, 0)->checkState() != Qt::Checked) continue;
        const auto directory = qobject_cast<QComboBox*>(table->cellWidget(i, 1))->currentData().toString();
        if (directory.isEmpty() || !QDir().mkpath(directory)) { errors << files[i].fileName() + QStringLiteral(": 无法创建分类目录"); continue; }
        const auto result = movePath(files[i].absoluteFilePath(), directory + '/' + files[i].fileName());
        if (result.ok) { ++success; recordMove(store, files[i].absoluteFilePath(), result.destination); }
        else errors << files[i].fileName() + ": " + result.error;
    }
    const auto message = QStringLiteral("已整理 %1 项，失败 %2 项").arg(success).arg(errors.size()); notify(message);
    if (!errors.isEmpty()) QMessageBox::warning(parent, QStringLiteral("部分项目未移动"), message + '\n' + errors.join('\n'));
}
QString kindTitle(const QString& kind) {
    const QMap<QString, QString> names{{"folder", QStringLiteral("文件格子")}, {"todo", QStringLiteral("待办")},
        {"note", QStringLiteral("随记")}, {"clock", QStringLiteral("时光")}, {"weather", QStringLiteral("天气")},
        {"music", QStringLiteral("音乐")}, {"search", QStringLiteral("搜索")}};
    return names.value(kind, kind);
}
void flushPages(QWidget* root) {
    if (auto* page = dynamic_cast<NotePage*>(root)) page->flushNow();
    for (auto* child : root->findChildren<QWidget*>()) if (auto* page = dynamic_cast<NotePage*>(child)) page->flushNow();
}
QWidget* createPage(Store& store, const QString& id, const Notify& notify) {
    const auto kind = store.widget(id)["kind"].toString();
    if (kind == "folder") return new FolderPage(store, id, notify);
    if (kind == "todo") return new TodoPage(store, notify);
    if (kind == "note") return new NotePage(store);
    if (kind == "search") return new SearchPage(store, notify);
    if (kind == "weather") return new WeatherPage(store, id);
    auto* page = new QWidget; auto* layout = new QVBoxLayout(page);
    if (kind == "clock") {
        auto* clock = new QLabel; clock->setAlignment(Qt::AlignCenter); clock->setStyleSheet("font-size:30px;"); layout->addWidget(clock);
        auto* date = new QLabel; date->setAlignment(Qt::AlignCenter); layout->addWidget(date);
        auto update = [clock, date] {
            clock->setText(QTime::currentTime().toString("HH:mm:ss"));
            const auto today = QDate::currentDate();
            date->setText(QLocale().toString(today, "yyyy年M月d日 dddd") + '\n' + lunarDate(today));
        };
        auto* timer = new QTimer(page); QObject::connect(timer, &QTimer::timeout, page, update); timer->start(1000); update();
    } else if (kind == "music") {
        auto* title = new QLabel(QStringLiteral("控制当前播放器")); title->setAlignment(Qt::AlignCenter); layout->addWidget(title);
        auto* controls = new QHBoxLayout;
        for (const auto& item : {qMakePair(QStringLiteral("上一首"), QStringLiteral("previous")), qMakePair(QStringLiteral("播放 / 暂停"), QStringLiteral("play")), qMakePair(QStringLiteral("下一首"), QStringLiteral("next"))})
            button(controls, item.first, [item, notify] { if (!mediaKey(item.second)) notify(QStringLiteral("媒体控制不可用")); });
        layout->addLayout(controls); auto* volume = new QSlider(Qt::Horizontal); volume->setRange(0, 100);
        const auto current = systemVolume(); volume->setEnabled(current >= 0); volume->setValue(qMax(0, current)); layout->addWidget(volume);
        QObject::connect(volume, &QSlider::sliderReleased, page, [volume, notify] { if (!setSystemVolume(volume->value())) notify(QStringLiteral("无法设置音量")); });
        layout->addWidget(new QLabel(QStringLiteral("系统音量")));
    }
    layout->addStretch(); return page;
}
} // namespace crossdesk::desktop
