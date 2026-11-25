#include <QCoreApplication>
#include "desktop/page_factories.h"
#include "desktop/search.h"
#include "desktop/search_apps.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDirIterator>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QPromise>
#include <QRegularExpression>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <atomic>
#include <algorithm>
#include "desktop/file_transfer.h"

namespace crossdesk::desktop {
namespace {
struct SearchBatch { QList<SearchHit> hits; int visited = 0; bool complete = false; };
class ResultRow final : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override {
        const int col = treeWidget()->sortColumn();
        if (col == 4) return data(4, Qt::UserRole).toLongLong() < other.data(4, Qt::UserRole).toLongLong();
        return QTreeWidgetItem::operator<(other);
    }
};
class SearchPage final : public QWidget {
public:
    SearchPage(Store& store, std::function<void(const QString&)> notify) : store_(store), notify_(std::move(notify)) {
        auto* layout = new QVBoxLayout(this); auto* row = new QHBoxLayout;
        history_ = new QComboBox; history_->setEditable(true); history_->setInsertPolicy(QComboBox::NoInsert);
        query_ = history_->lineEdit(); query_->setObjectName("searchQuery");
        query_->setPlaceholderText(QCoreApplication::translate("CrossDesk", "搜索文件、应用、设置、待办、随记")); row->addWidget(history_, 1);
        auto* favorite = new QPushButton(QCoreApplication::translate("CrossDesk", "收藏查询")); row->addWidget(favorite); layout->addLayout(row);
        auto* options = new QHBoxLayout; filter_ = new QComboBox;
        for (const auto& pair : QList<QPair<QString, QString>>{{QCoreApplication::translate("CrossDesk", "全部"), ""}, {QCoreApplication::translate("CrossDesk", "文件"), "file"},
            {QCoreApplication::translate("CrossDesk", "文件夹"), "folder"}, {QCoreApplication::translate("CrossDesk", "应用"), "app"}, {QCoreApplication::translate("CrossDesk", "设置"), "settings"},
            {QCoreApplication::translate("CrossDesk", "待办"), "todo"}, {QCoreApplication::translate("CrossDesk", "随记"), "note"}}) filter_->addItem(pair.first, pair.second);
        filter_->setObjectName("searchFilter"); options->addWidget(filter_);
        auto* favorites = new QPushButton(QCoreApplication::translate("CrossDesk", "收藏夹")); options->addWidget(favorites);
        auto* roots = new QPushButton(QCoreApplication::translate("CrossDesk", "搜索目录")); options->addWidget(roots);
        auto* clear = new QPushButton(QCoreApplication::translate("CrossDesk", "清除历史")); options->addWidget(clear); layout->addLayout(options);
        status_ = new QLabel; status_->setWordWrap(true); layout->addWidget(status_);
        results_ = new QTreeWidget; results_->setObjectName("searchResults"); results_->setColumnCount(5);
        results_->setHeaderLabels({QCoreApplication::translate("CrossDesk", "名称"), QCoreApplication::translate("CrossDesk", "类型"), QCoreApplication::translate("CrossDesk", "位置"), QCoreApplication::translate("CrossDesk", "修改时间"), QCoreApplication::translate("CrossDesk", "大小")});
        results_->setRootIsDecorated(false); results_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        results_->header()->setSectionResizeMode(QHeaderView::Interactive); results_->header()->setStretchLastSection(false);
        results_->setColumnWidth(0, 190); results_->setColumnWidth(2, 220); results_->setSortingEnabled(true); results_->sortByColumn(0, Qt::AscendingOrder);
        results_->setContextMenuPolicy(Qt::CustomContextMenu); layout->addWidget(results_, 1);
        auto* hint = new QLabel(QCoreApplication::translate("CrossDesk", "语法：ext:pdf、type:folder、name:报告、\"完整短语\"、-排除词；Ctrl/Shift 多选"));
        hint->setWordWrap(true); layout->addWidget(hint);
        debounce_.setSingleShot(true); debounce_.setInterval(300);
        connect(query_, &QLineEdit::textChanged, this, [this] { debounce_.start(); });
        connect(query_, &QLineEdit::returnPressed, this, [this] { saveHistory(); search(); });
        connect(filter_, &QComboBox::currentIndexChanged, this, [this] { search(); });
        connect(&debounce_, &QTimer::timeout, this, [this] { search(); });
        connect(favorite, &QPushButton::clicked, this, [this] {
            const auto text = query_->text().trimmed(); if (text.isEmpty()) return;
            auto settings = store_.settings(); auto values = settings["searchFavorites"].toArray();
            if (!values.contains(text)) values.append(text); settings["searchFavorites"] = values; store_.setSettings(settings);
        });
        connect(favorites, &QPushButton::clicked, this, [this, favorites] {
            QMenu menu; for (const auto& value : store_.settings()["searchFavorites"].toArray()) {
                auto* action = menu.addAction(value.toString()); connect(action, &QAction::triggered, this, [this, value] { query_->setText(value.toString()); });
            }
            menu.addSeparator(); auto* remove = menu.addAction(QCoreApplication::translate("CrossDesk", "移除当前查询"));
            connect(remove, &QAction::triggered, this, [this] {
                auto settings = store_.settings(); auto values = settings["searchFavorites"].toArray();
                for (int i = values.size() - 1; i >= 0; --i) if (values[i].toString() == query_->text().trimmed()) values.removeAt(i);
                settings["searchFavorites"] = values; store_.setSettings(settings);
            }); menu.exec(favorites->mapToGlobal(QPoint(0, favorites->height())));
        });
        connect(clear, &QPushButton::clicked, this, [this] { auto s = store_.settings(); s["searchHistory"] = QJsonArray{}; store_.setSettings(s); reloadHistory(); });
        connect(roots, &QPushButton::clicked, this, [this] { editRoots(); });
        connect(results_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { open(item); });
        connect(results_, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) { context(position); });
        reloadHistory();
    }
    ~SearchPage() override { if (cancel_) cancel_->store(true); }
private:
    void reloadHistory() {
        const auto current = query_->text(); QSignalBlocker block(history_); history_->clear();
        for (const auto& value : store_.settings()["searchHistory"].toArray()) history_->addItem(value.toString());
        history_->setEditText(current);
    }
    void saveHistory() {
        const auto text = query_->text().trimmed(); if (text.isEmpty()) return;
        auto settings = store_.settings(); QJsonArray history{text};
        for (const auto& value : settings["searchHistory"].toArray()) if (value.toString() != text && history.size() < 30) history.append(value);
        settings["searchHistory"] = history; store_.setSettings(settings); reloadHistory();
    }
    void add(const QList<SearchHit>& batch, quint64 generation) {
        if (generation != generation_) return;
        const auto filter = filter_->currentData().toString();
        for (const auto& hit : batch) {
            if ((!filter.isEmpty() && hit.kind != filter) || count_ >= maximum_) continue;
            const auto key = hit.kind + '|' + hit.location + '|' + hit.id; if (seen_.contains(key)) continue; seen_.insert(key);
            auto* item = new ResultRow(results_); item->setText(0, hit.name); item->setText(1, hit.kind);
            item->setText(2, hit.location); item->setText(3, hit.modified.toString("yyyy-MM-dd HH:mm"));
            if (hit.kind == "file") item->setText(4, QLocale().formattedDataSize(hit.size));
            item->setData(4, Qt::UserRole, hit.size); item->setData(0, Qt::UserRole, hit.id); item->setToolTip(2, hit.location); ++count_;
        }
        status_->setText(QCoreApplication::translate("CrossDesk", "已找到 %1 项，继续搜索…").arg(count_));
    }
    void search() {
        if (cancel_) cancel_->store(true);
        cancel_ = std::make_shared<std::atomic_bool>(false); const auto canceled = cancel_;
        const auto generation = ++generation_; results_->clear(); seen_.clear(); count_ = 0;
        const auto query = query_->text().trimmed();
        if (query.isEmpty()) { status_->setText(QCoreApplication::translate("CrossDesk", "输入关键词开始搜索")); return; }
        maximum_ = std::clamp(store_.settings()["searchLimit"].toInt(200), 20, 2000);
        QList<SearchHit> local;
        for (const auto* key : {"todos", "notes"}) for (const auto& value : store_.records(key)) {
            const auto record = value.toObject(); const auto kind = QString(key) == "todos" ? QString("todo") : QString("note");
            SearchHit hit{record["title"].toString(), kind, record["body"].toString() + record["notes"].toString(), record["id"].toString(), {}, 0};
            if (matchesSearch(hit, query)) local << hit;
        }
        for (const auto& hit : systemSearchEntries()) if (matchesSearch(hit, query)) local << hit;
        add(local, generation);
        auto sourceKind = filter_->currentData().toString();
        if (sourceKind.isEmpty()) {
            const auto typed = QRegularExpression("(?:^|\\s)type:(file|folder|app|settings|todo|note)(?:\\s|$)", QRegularExpression::CaseInsensitiveOption).match(query);
            if (typed.hasMatch()) sourceKind = typed.captured(1).toLower();
        }
        if (sourceKind == "todo" || sourceKind == "note" || sourceKind == "settings") {
            status_->setText(QString::number(count_) + QCoreApplication::translate("CrossDesk", " 项结果")); return;
        }
        QStringList roots;
        for (const auto& item : store_.widgets()) if (item.toObject()["kind"] == "folder") roots << item.toObject()["data"].toObject()["path"].toString();
        for (const auto& value : store_.settings()["searchRoots"].toArray()) roots << value.toString();
        for (const auto location : {QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation, QStandardPaths::DownloadLocation})
            roots << QStandardPaths::writableLocation(location);
        roots.removeAll({}); roots.removeDuplicates();
        const auto limit = maximum_;
        auto* watcher = new QFutureWatcher<SearchBatch>(this);
        connect(watcher, &QFutureWatcher<SearchBatch>::resultReadyAt, this, [this, watcher, generation](int index) {
            if (generation != generation_) return;
            const auto batch = watcher->resultAt(index); add(batch.hits, generation);
            if (batch.complete) status_->setText(QCoreApplication::translate("CrossDesk", "%1 项结果 · 检查 %2 个文件项目%3").arg(count_).arg(batch.visited)
                .arg(count_ >= maximum_ ? QCoreApplication::translate("CrossDesk", "（达到数量上限）") : QString{}));
        });
        connect(watcher, &QFutureWatcherBase::finished, watcher, &QObject::deleteLater);
        watcher->setFuture(QtConcurrent::run([canceled, roots, query, limit, sourceKind](QPromise<SearchBatch>& promise) {
            auto send = [&promise](QList<SearchHit> batch) { promise.addResult(SearchBatch{std::move(batch), 0, false}); };
            QList<SearchHit> batch; int found = 0, visited = 0;
            if (sourceKind.isEmpty() || sourceKind == "app") for (const auto& hit : installedApplications()) {
                if (canceled->load()) return;
                if (matchesSearch(hit, query)) batch << hit;
            }
            send(batch); batch.clear();
            if (sourceKind == "app") { promise.addResult(SearchBatch{{}, 0, true}); return; }
            for (const auto& root : roots) {
                if (canceled->load() || found >= limit) break;
                QDirIterator files(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
                while (!canceled->load() && files.hasNext() && visited < 250000 && found < limit) {
                    const auto path = files.next(); ++visited; const auto info = files.fileInfo();
                    SearchHit hit{info.fileName(), info.isDir() ? QString("folder") : QString("file"), path, {}, info.lastModified(), info.size()};
                    if (!sourceKind.isEmpty() && hit.kind != sourceKind) continue;
                    if (!matchesSearch(hit, query)) continue;
                    batch << hit; ++found;
                    if (batch.size() >= 24) { send(batch); batch.clear(); }
                }
                if (!batch.isEmpty()) { send(batch); batch.clear(); }
            }
            promise.addResult(SearchBatch{{}, visited, true});
        }));
    }
    void open(QTreeWidgetItem* item) {
        saveHistory(); const auto kind = item->text(1); const auto location = item->text(2);
        if (kind == "todo" || kind == "note") {
            auto* dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(item->text(0)); dialog->resize(800, 600);
            auto* layout = new QVBoxLayout(dialog); auto* page = createRecordPage(store_, kind, notify_); layout->addWidget(page);
            if (auto* list = page->findChild<QListWidget*>(kind == "todo" ? "todoList" : "noteList"))
                for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole) == item->data(0, Qt::UserRole)) { list->setCurrentRow(i); break; }
            dialog->show();
        } else if (kind == "app") { if (!openApplication(location)) notify_(QCoreApplication::translate("CrossDesk", "无法打开应用")); }
        else if (!QDesktopServices::openUrl(kind == "settings" ? QUrl(location) : QUrl::fromLocalFile(location))) notify_(QCoreApplication::translate("CrossDesk", "无法打开结果"));
    }
    void editRoots() {
        QDialog dialog(this); dialog.setWindowTitle(QCoreApplication::translate("CrossDesk", "额外搜索目录")); auto* layout = new QVBoxLayout(&dialog);
        auto* list = new QListWidget; layout->addWidget(list);
        for (const auto& value : store_.settings()["searchRoots"].toArray()) list->addItem(value.toString());
        auto* addButton = new QPushButton(QCoreApplication::translate("CrossDesk", "添加目录")); layout->addWidget(addButton);
        auto* remove = new QPushButton(QCoreApplication::translate("CrossDesk", "移除所选目录")); layout->addWidget(remove);
        auto* limit = new QComboBox; for (int count : {50, 100, 200, 500, 1000, 2000}) limit->addItem(QString::number(count), count);
        limit->setCurrentIndex(qMax(0, limit->findData(store_.settings()["searchLimit"].toInt(200)))); layout->addWidget(new QLabel(QCoreApplication::translate("CrossDesk", "结果数量上限"))); layout->addWidget(limit);
        auto* save = new QPushButton(QCoreApplication::translate("CrossDesk", "保存")); layout->addWidget(save);
        connect(addButton, &QPushButton::clicked, &dialog, [&] { const auto path = QFileDialog::getExistingDirectory(&dialog); if (!path.isEmpty() && list->findItems(path, Qt::MatchExactly).isEmpty()) list->addItem(path); });
        connect(remove, &QPushButton::clicked, &dialog, [&] { delete list->takeItem(list->currentRow()); });
        connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
        if (dialog.exec() != QDialog::Accepted) return;
        QJsonArray values; for (int i = 0; i < list->count(); ++i) values.append(list->item(i)->text());
        auto settings = store_.settings(); settings["searchRoots"] = values; settings["searchLimit"] = limit->currentData().toInt(); store_.setSettings(settings); search();
    }
    void context(const QPoint& point) {
        const auto rows = results_->selectedItems(); if (rows.isEmpty()) return;
        QMenu menu; menu.addAction(QCoreApplication::translate("CrossDesk", "打开所选"), this, [this, rows] { for (auto* row : rows) open(row); });
        menu.addAction(QCoreApplication::translate("CrossDesk", "复制位置"), this, [rows] { QStringList paths; for (auto* row : rows) paths << row->text(2); QApplication::clipboard()->setText(paths.join('\n')); });
        menu.addAction(QCoreApplication::translate("CrossDesk", "复制文件"), this, [rows] {
            QList<QUrl> urls; for (auto* row : rows) if (row->text(1) == "file" || row->text(1) == "folder") urls << QUrl::fromLocalFile(row->text(2));
            if (urls.isEmpty()) return; auto* mime = new QMimeData; mime->setUrls(urls); QApplication::clipboard()->setMimeData(mime);
        });
        menu.addAction(QCoreApplication::translate("CrossDesk", "在文件夹中显示"), this, [rows] { for (auto* row : rows) if (row->text(1) == "file" || row->text(1) == "folder") QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(row->text(2)).absolutePath())); });
        for (bool move : {false, true}) menu.addAction(move ? QCoreApplication::translate("CrossDesk", "移动到文件夹…") : QCoreApplication::translate("CrossDesk", "复制到文件夹…"), this, [this, rows, move] {
            QStringList files; for (auto* row : rows) if (row->text(1) == "file" || row->text(1) == "folder") files << row->text(2);
            if (files.isEmpty()) return;
            const auto destination = QFileDialog::getExistingDirectory(this, QCoreApplication::translate("CrossDesk", "目标目录")); if (destination.isEmpty()) return;
            const auto result = transferFiles(store_, files, destination, move, this);
            notify_(QCoreApplication::translate("CrossDesk", "已处理 %1 项，失败 %2 项").arg(result.completed).arg(result.errors.size()));
            if (move) search();
        });
        menu.addAction(QCoreApplication::translate("CrossDesk", "文件移到回收站"), this, [this, rows] {
            QStringList files; for (auto* row : rows) if (row->text(1) == "file" || row->text(1) == "folder") files << row->text(2);
            if (files.isEmpty() || QMessageBox::question(this, QCoreApplication::translate("CrossDesk", "确认回收"), QCoreApplication::translate("CrossDesk", "将 %1 个文件项目移到回收站？").arg(files.size())) != QMessageBox::Yes) return;
            int failed = 0; for (const auto& file : files) if (!QFile::moveToTrash(file)) ++failed;
            notify_(QCoreApplication::translate("CrossDesk", "已处理 %1 项，失败 %2 项").arg(files.size() - failed).arg(failed)); search();
        }); menu.exec(results_->viewport()->mapToGlobal(point));
    }
    Store& store_; std::function<void(const QString&)> notify_; QComboBox *history_, *filter_;
    QLineEdit* query_; QLabel* status_; QTreeWidget* results_; QTimer debounce_;
    quint64 generation_ = 0; std::shared_ptr<std::atomic_bool> cancel_; QSet<QString> seen_; int count_ = 0, maximum_ = 200;
};
}
QWidget* createSearchPage(Store& store, const std::function<void(const QString&)>& notify) { return new SearchPage(store, notify); }
}
