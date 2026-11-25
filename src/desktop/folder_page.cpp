#include <QCoreApplication>
#include "desktop/page_factories.h"
#include "desktop/file_transfer.h"
#include "desktop/shell.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QTemporaryFile>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace crossdesk::desktop {
namespace {
using Notify = std::function<void(const QString&)>;
QPushButton* button(QBoxLayout* layout, const QString& text, const std::function<void()>& action) {
    auto* result = new QPushButton(text); layout->addWidget(result);
    QObject::connect(result, &QPushButton::clicked, result, action); return result;
}
class DropModel final : public QFileSystemModel {
public:
    explicit DropModel(QObject* parent) : QFileSystemModel(parent) { setReadOnly(false); setObjectName("folderFileModel"); }
    std::function<bool(const QList<QUrl>&, const QString&, Qt::DropAction)> transfer;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int, int, const QModelIndex& parent) override {
        if (action == Qt::IgnoreAction) return true;
        if (!data->hasUrls() || !transfer) return false;
        auto directory = filePath(parent);
        if (!QFileInfo(directory).isDir()) directory = QFileInfo(directory).absolutePath();
        return transfer(data->urls(), directory, action);
    }
};
class FileProxy final : public QSortFilterProxyModel {
public:
    explicit FileProxy(QObject* parent) : QSortFilterProxyModel(parent) { setObjectName("folderSortModel"); setDynamicSortFilter(true); }
    QStringList order; QString category; bool hideNames = false, manual = false;
    void update() { invalidate(); }
    QVariant data(const QModelIndex& index, int role) const override {
        return hideNames && role == Qt::DisplayRole ? QVariant{} : QSortFilterProxyModel::data(index, role);
    }
protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        if (!manual) return QSortFilterProxyModel::lessThan(left, right);
        const auto* files = static_cast<QFileSystemModel*>(sourceModel());
        int a = order.indexOf(files->fileName(left)), b = order.indexOf(files->fileName(right));
        if (a < 0) a = order.size(); if (b < 0) b = order.size();
        return a != b ? a < b : QString::localeAwareCompare(files->fileName(left), files->fileName(right)) < 0;
    }
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (category.isEmpty()) return true;
        const auto* files = static_cast<QFileSystemModel*>(sourceModel());
        const auto index = files->index(row, 0, parent);
        // Directory ancestors must remain addressable; category groups files.
        return files->isDir(index) || categoryFor(files->filePath(index)) == category;
    }
};
class FolderPage final : public QWidget {
public:
    FolderPage(Store& store, QString id, Notify notify) : store_(store), id_(std::move(id)), notify_(std::move(notify)) {
        auto* layout = new QVBoxLayout(this);
        path_ = new QLineEdit(config()["path"].toString()); path_->setObjectName("folderPath");
        path_->setPlaceholderText(QCoreApplication::translate("CrossDesk", "选择要映射的文件夹")); layout->addWidget(path_);
        auto* tools = new QHBoxLayout; layout->addLayout(tools);
        button(tools, QCoreApplication::translate("CrossDesk", "选择"), [this] { const auto path = QFileDialog::getExistingDirectory(this, QCoreApplication::translate("CrossDesk", "映射文件夹"), path_->text()); if (!path.isEmpty()) navigate(path, true); });
        button(tools, QCoreApplication::translate("CrossDesk", "上级"), [this] { navigate(QFileInfo(path_->text()).absolutePath(), false); });
        button(tools, QCoreApplication::translate("CrossDesk", "新建"), [this] { newFolder(); });
        button(tools, QCoreApplication::translate("CrossDesk", "叠放"), [this] { stacks(); })->setObjectName("folderStacks");
        button(tools, QCoreApplication::translate("CrossDesk", "视图"), [this] { viewPreferences(); })->setObjectName("folderPreferences");
        auto* modes = new QHBoxLayout; layout->addLayout(modes);
        mode_ = new QComboBox; mode_->setObjectName("folderMode"); mode_->addItems({QCoreApplication::translate("CrossDesk", "图标"), QCoreApplication::translate("CrossDesk", "列表")}); modes->addWidget(mode_);
        sort_ = new QComboBox; sort_->setObjectName("folderSort"); sort_->addItems({QCoreApplication::translate("CrossDesk", "名称"), QCoreApplication::translate("CrossDesk", "大小"), QCoreApplication::translate("CrossDesk", "类型"), QCoreApplication::translate("CrossDesk", "日期"), QCoreApplication::translate("CrossDesk", "手动顺序")}); modes->addWidget(sort_);
        view_ = new QListView; view_->setObjectName("folderView"); model_ = new DropModel(this); proxy_ = new FileProxy(this); proxy_->setSourceModel(model_); view_->setModel(proxy_);
        view_->setResizeMode(QListView::Adjust); view_->setSelectionMode(QAbstractItemView::ExtendedSelection); view_->setDragDropMode(QAbstractItemView::DragDrop);
        view_->setDefaultDropAction(Qt::CopyAction); view_->setContextMenuPolicy(Qt::CustomContextMenu); layout->addWidget(view_, 1);
        mode_->setCurrentIndex(config()["viewMode"].toInt()); sort_->setCurrentIndex(config()["sort"].toInt());
        model_->transfer = [this](const QList<QUrl>& urls, const QString& target, Qt::DropAction action) { return importUrls(urls, target, action); };
        connect(mode_, &QComboBox::currentIndexChanged, this, [this](int value) { put("viewMode", value); applyView(); });
        connect(sort_, &QComboBox::currentIndexChanged, this, [this](int value) { put("sort", value); applyView(); });
        connect(path_, &QLineEdit::returnPressed, this, [this] { navigate(path_->text(), true); });
        connect(view_, &QListView::doubleClicked, this, [this](const QModelIndex& index) { const auto path = model_->filePath(proxy_->mapToSource(index)); if (QFileInfo(path).isDir()) navigate(path, false); else QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
        connect(view_, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) { context(view_, point); });
        auto shortcut = [this](const QKeySequence& key, const std::function<void()>& action) { auto* command = new QShortcut(key, this); command->setContext(Qt::WidgetWithChildrenShortcut); connect(command, &QShortcut::activated, this, action); };
        shortcut(QKeySequence::Copy, [this] { clipboard(view_, false); }); shortcut(QKeySequence::Cut, [this] { clipboard(view_, true); });
        shortcut(QKeySequence::Paste, [this] { paste(); }); shortcut(QKeySequence(Qt::Key_F2), [this] { view_->edit(view_->currentIndex()); });
        shortcut(QKeySequence(Qt::Key_Delete), [this] { trash(selected(view_)); });
        navigate(path_->text(), false); applyView();
    }
private:
    QJsonObject config() const { return store_.widget(id_)["data"].toObject(); }
    void put(const QString& key, const QJsonValue& value) { auto widget = store_.widget(id_); auto data = widget["data"].toObject(); data[key] = value; widget["data"] = data; store_.putWidget(widget); }
    void applyView() {
        const auto data = config();
        model_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | (data["hiddenFiles"].toBool() ? QDir::Hidden | QDir::System : QDir::Filters{}));
        view_->setViewMode(mode_->currentIndex() ? QListView::ListMode : QListView::IconMode);
        const int size = std::clamp(data["iconSize"].toInt(48), 24, 128), density = data["density"].toInt(1), lines = data["nameLines"].toInt(2);
        view_->setIconSize(QSize(size, size)); view_->setSpacing(density == 0 ? 2 : density == 2 ? 14 : 7);
        view_->setWordWrap(lines == 2); view_->setTextElideMode(Qt::ElideRight);
        view_->setGridSize(mode_->currentIndex() ? QSize{} : QSize(qMax(92, size + 34), size + (lines == 0 ? 15 : lines == 1 ? 36 : 54)));
        proxy_->hideNames = lines == 0; proxy_->manual = sort_->currentIndex() == 4; proxy_->order.clear();
        for (const auto& value : data["orders"].toObject()[path_->text()].toArray()) proxy_->order << value.toString();
        proxy_->update(); proxy_->sort(proxy_->manual ? 0 : sort_->currentIndex());
    }
    void navigate(const QString& path, bool persist) {
        if (!QFileInfo(path).isDir()) { notify_(QCoreApplication::translate("CrossDesk", "文件夹不存在，请重新选择")); return; }
        path_->setText(QFileInfo(path).absoluteFilePath()); view_->setRootIndex(proxy_->mapFromSource(model_->setRootPath(path_->text())));
        if (persist) put("path", path_->text()); applyView();
    }
    QStringList selected(QListView* view) const {
        QStringList files; auto* proxy = static_cast<QSortFilterProxyModel*>(view->model());
        for (const auto& index : view->selectionModel()->selectedRows()) files << model_->filePath(proxy->mapToSource(index)); return files;
    }
    void newFolder() {
        const auto name = QInputDialog::getText(this, QCoreApplication::translate("CrossDesk", "新建文件夹"), QCoreApplication::translate("CrossDesk", "名称")); if (name.isEmpty()) return;
        if (name.contains('/') || name.contains('\\') || name == "." || name == ".." || !QDir(path_->text()).mkdir(name)) { notify_(QCoreApplication::translate("CrossDesk", "无法新建文件夹")); return; }
        QTimer::singleShot(120, this, [this, name] { const auto index = proxy_->mapFromSource(model_->index(path_->text() + '/' + name)); view_->setCurrentIndex(index); view_->scrollTo(index); view_->edit(index); });
    }
    void clipboard(QListView* view, bool cut) {
        const auto files = selected(view); if (files.isEmpty()) return;
        auto* mime = new QMimeData; QList<QUrl> urls; for (const auto& path : files) urls << QUrl::fromLocalFile(path);
        mime->setUrls(urls); mime->setData("application/x-crossdesk-cut", cut ? "1" : "0"); QByteArray effect(4, '\0'); effect[0] = cut ? 2 : 1;
        mime->setData("application/x-qt-windows-mime;value=\"Preferred DropEffect\"", effect); QApplication::clipboard()->setMimeData(mime);
    }
    void paste() {
        const auto* mime = QApplication::clipboard()->mimeData(); bool move = mime->data("application/x-crossdesk-cut") == "1";
        const auto effect = mime->data("application/x-qt-windows-mime;value=\"Preferred DropEffect\""); if (!effect.isEmpty() && effect[0] == 2) move = true;
        if (importUrls(mime->urls(), path_->text(), move ? Qt::MoveAction : Qt::CopyAction) && move) QApplication::clipboard()->clear();
    }
    bool importUrls(const QList<QUrl>& urls, const QString& destination, Qt::DropAction action) {
        QStringList locals; bool ok = true;
        for (const auto& url : urls) {
            if (!url.isLocalFile()) { ok = download(url, destination) && ok; continue; }
            const auto source = url.toLocalFile(), target = destination + '/' + QFileInfo(source).fileName();
            if (QFileInfo::exists(target)) { notify_(QCoreApplication::translate("CrossDesk", "目标已存在，未覆盖：") + target); ok = false; continue; }
            if (action == Qt::LinkAction) { QString error; if (!createFileShortcut(source, target + ".lnk", &error)) { notify_(error); ok = false; } }
            else locals << source;
        }
        if (!locals.isEmpty()) {
            const auto summary = transferFiles(store_, locals, destination, action == Qt::MoveAction, this);
            notify_(QCoreApplication::translate("CrossDesk", "完成 %1 项%2").arg(summary.completed).arg(summary.cancelled ? QCoreApplication::translate("CrossDesk", "，其余已取消") : summary.errors.isEmpty() ? QString{} : QCoreApplication::translate("CrossDesk", "，部分失败")));
            ok = summary.completed == locals.size() && !summary.cancelled && summary.errors.isEmpty() && ok;
        }
        return ok;
    }
    bool download(const QUrl& url, const QString& directory) {
        if ((url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty() || !url.userInfo().isEmpty()) { notify_(QCoreApplication::translate("CrossDesk", "仅支持 HTTP/HTTPS 文件链接")); return false; }
        if (QMessageBox::question(this, QCoreApplication::translate("CrossDesk", "下载并导入文件"), QCoreApplication::translate("CrossDesk", "从此地址下载文件到当前文件夹？\n%1").arg(url.toDisplayString())) != QMessageBox::Yes) return false;
        auto name = QFileInfo(url.path()).fileName(); if (name.isEmpty()) name = "download";
        name = QInputDialog::getText(this, QCoreApplication::translate("CrossDesk", "保存下载文件"), QCoreApplication::translate("CrossDesk", "文件名"), QLineEdit::Normal, name);
        if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\\') || name.contains(':')) return false;
        const auto target = directory + '/' + name; if (QFileInfo::exists(target)) { notify_(QCoreApplication::translate("CrossDesk", "目标已存在，未下载")); return false; }
        QTemporaryFile output(target + ".download-XXXXXX"); if (!output.open()) { notify_(output.errorString()); return false; }
        QNetworkAccessManager network; QNetworkRequest request(url); request.setTransferTimeout(30000); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto* reply = network.get(request); QProgressDialog progress(QCoreApplication::translate("CrossDesk", "下载文件"), QCoreApplication::translate("CrossDesk", "取消"), 0, 1000, this);
        progress.setAutoClose(false); progress.setAutoReset(false); bool cancelled = false; QString error; qint64 bytes = 0;
        connect(&progress, &QProgressDialog::canceled, &progress, [&] { cancelled = true; reply->abort(); });
        connect(reply, &QNetworkReply::readyRead, &progress, [&] { const auto chunk = reply->readAll(); bytes += chunk.size(); if (bytes > 256LL * 1024 * 1024 || output.write(chunk) != chunk.size()) { error = QCoreApplication::translate("CrossDesk", "下载超过 256 MiB 或无法写入目标"); reply->abort(); } });
        connect(reply, &QNetworkReply::downloadProgress, &progress, [&](qint64 received, qint64 total) { progress.setValue(total > 0 ? int(qMin<qint64>(1000, received * 1000 / total)) : 0); progress.setLabelText(QCoreApplication::translate("CrossDesk", "已下载 %1 KiB").arg(received / 1024)); });
        connect(reply, &QNetworkReply::finished, &progress, &QDialog::accept); progress.exec();
        if (reply->isRunning()) { cancelled = true; reply->abort(); }
        if (cancelled || !error.isEmpty() || reply->error() != QNetworkReply::NoError) { notify_(cancelled ? QCoreApplication::translate("CrossDesk", "下载已取消") : error.isEmpty() ? reply->errorString() : error); return false; }
        if (!output.flush()) { notify_(output.errorString()); return false; } output.close();
        if (!output.rename(target)) { notify_(QCoreApplication::translate("CrossDesk", "无法保存下载，目标可能已存在")); return false; } output.setAutoRemove(false); notify_(QCoreApplication::translate("CrossDesk", "下载完成，文件未自动打开")); return true;
    }
    void trash(const QStringList& files) {
        if (files.isEmpty() || QMessageBox::question(this, QCoreApplication::translate("CrossDesk", "移到回收站"), QCoreApplication::translate("CrossDesk", "将选中的 %1 个项目移到回收站？").arg(files.size())) != QMessageBox::Yes) return;
        for (const auto& file : files) if (!QFile::moveToTrash(file)) notify_(QCoreApplication::translate("CrossDesk", "无法移到回收站：") + file);
    }
    void moveOrder(QListView* view, int offset) {
        if (selected(view).size() != 1 || view != view_) return;
        QStringList names; for (int i = 0; i < proxy_->rowCount(view_->rootIndex()); ++i) names << model_->fileName(proxy_->mapToSource(proxy_->index(i, 0, view_->rootIndex())));
        const int row = names.indexOf(QFileInfo(selected(view).first()).fileName()), next = row + offset;
        if (row < 0 || next < 0 || next >= names.size()) return;
        names.swapItemsAt(row, next); QJsonArray order; for (const auto& item : names) order.append(item);
        auto orders = config()["orders"].toObject(); orders[path_->text()] = order; put("orders", orders); sort_->setCurrentIndex(4); applyView();
    }
    void context(QListView* view, const QPoint& point) {
        QMenu menu; const auto files = selected(view);
        if (!files.isEmpty()) {
            menu.addAction(QCoreApplication::translate("CrossDesk", "打开"), this, [files] { for (const auto& path : files) QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
            menu.addAction(QCoreApplication::translate("CrossDesk", "复制"), this, [this, view] { clipboard(view, false); }); menu.addAction(QCoreApplication::translate("CrossDesk", "剪切"), this, [this, view] { clipboard(view, true); });
            menu.addAction(QCoreApplication::translate("CrossDesk", "重命名"), this, [view] { view->edit(view->currentIndex()); });
            menu.addAction(QCoreApplication::translate("CrossDesk", "前移"), this, [this, view] { moveOrder(view, -1); }); menu.addAction(QCoreApplication::translate("CrossDesk", "后移"), this, [this, view] { moveOrder(view, 1); });
            menu.addAction(QCoreApplication::translate("CrossDesk", "移至…"), this, [this, files] { const auto directory = QFileDialog::getExistingDirectory(this, QCoreApplication::translate("CrossDesk", "移动到文件夹")); if (!directory.isEmpty()) transferFiles(store_, files, directory, true, this); });
            menu.addAction(QCoreApplication::translate("CrossDesk", "创建快捷方式"), this, [this, files] { for (const auto& path : files) { QString error; if (!createFileShortcut(path, path + ".lnk", &error)) notify_(error); } });
            menu.addAction(QCoreApplication::translate("CrossDesk", "移到回收站"), this, [this, files] { trash(files); }); menu.addSeparator();
            menu.addAction(QCoreApplication::translate("CrossDesk", "打开方式"), this, [this, files] { if (!openWith(files.first(), winId())) notify_(QCoreApplication::translate("CrossDesk", "当前文件不支持打开方式")); });
            menu.addAction(QCoreApplication::translate("CrossDesk", "属性"), this, [this, files] { if (!fileProperties(files.first(), winId())) notify_(QCoreApplication::translate("CrossDesk", "无法显示文件属性")); });
            menu.addAction(QCoreApplication::translate("CrossDesk", "系统右键菜单"), this, [this, files] { if (!systemFileMenu(files.first(), winId(), QCursor::pos())) notify_(QCoreApplication::translate("CrossDesk", "系统菜单不可用")); });
        }
        menu.addAction(QCoreApplication::translate("CrossDesk", "粘贴"), this, [this] { paste(); });
        menu.addAction(QCoreApplication::translate("CrossDesk", "导入文件…"), this, [this] { const auto files = QFileDialog::getOpenFileNames(this, QCoreApplication::translate("CrossDesk", "导入文件")); if (!files.isEmpty()) transferFiles(store_, files, path_->text(), false, this); });
        menu.addAction(QCoreApplication::translate("CrossDesk", "下载链接…"), this, [this] { const auto text = QInputDialog::getText(this, QCoreApplication::translate("CrossDesk", "下载链接"), QCoreApplication::translate("CrossDesk", "HTTP/HTTPS 地址")); if (!text.isEmpty()) download(QUrl::fromUserInput(text), path_->text()); });
        menu.addAction(QCoreApplication::translate("CrossDesk", "在资源管理器中打开"), this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(path_->text())); });
        menu.addAction(QCoreApplication::translate("CrossDesk", "整理当前文件夹…"), this, [this] { organizeFolder(store_, path_->text(), this, notify_); });
        menu.exec(view->viewport()->mapToGlobal(point));
    }
    void viewPreferences() {
        QDialog dialog(this); dialog.setWindowTitle(QCoreApplication::translate("CrossDesk", "文件格子视图")); auto* form = new QFormLayout(&dialog);
        auto* size = new QSlider(Qt::Horizontal); size->setRange(24, 128); size->setValue(config()["iconSize"].toInt(48)); form->addRow(QCoreApplication::translate("CrossDesk", "图标大小"), size);
        auto* density = new QComboBox; density->addItems({QCoreApplication::translate("CrossDesk", "紧凑"), QCoreApplication::translate("CrossDesk", "标准"), QCoreApplication::translate("CrossDesk", "宽松")}); density->setCurrentIndex(config()["density"].toInt(1)); form->addRow(QCoreApplication::translate("CrossDesk", "密度"), density);
        auto* names = new QComboBox; names->addItems({QCoreApplication::translate("CrossDesk", "隐藏名称"), QCoreApplication::translate("CrossDesk", "单行"), QCoreApplication::translate("CrossDesk", "双行")}); names->setCurrentIndex(config()["nameLines"].toInt(2)); form->addRow(QCoreApplication::translate("CrossDesk", "文件名"), names);
        auto* hidden = new QCheckBox(QCoreApplication::translate("CrossDesk", "显示隐藏文件")); hidden->setChecked(config()["hiddenFiles"].toBool()); form->addRow(hidden);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); if (dialog.exec() != QDialog::Accepted) return;
        auto data = config(); data["iconSize"] = size->value(); data["density"] = density->currentIndex(); data["nameLines"] = names->currentIndex(); data["hiddenFiles"] = hidden->isChecked();
        auto widget = store_.widget(id_); widget["data"] = data; store_.putWidget(widget); applyView();
    }
    void stacks() {
        QDialog dialog(this); dialog.setWindowTitle(QCoreApplication::translate("CrossDesk", "文件叠放")); dialog.resize(600, 440); auto* layout = new QVBoxLayout(&dialog);
        auto* categories = new QComboBox; categories->addItem(QCoreApplication::translate("CrossDesk", "全部"), ""); QMap<QString, int> counts;
        for (const auto& file : QDir(path_->text()).entryInfoList(model_->filter())) ++counts[categoryFor(file.absoluteFilePath())];
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) categories->addItem(it.key() + QStringLiteral("（%1）").arg(it.value()), it.key()); layout->addWidget(categories);
        auto* files = new QListView; auto* proxy = new FileProxy(files); proxy->setSourceModel(model_); files->setModel(proxy); files->setRootIndex(proxy->mapFromSource(model_->index(path_->text())));
        files->setViewMode(QListView::IconMode); files->setResizeMode(QListView::Adjust); files->setIconSize(view_->iconSize()); files->setSelectionMode(QAbstractItemView::ExtendedSelection);
        files->setDragDropMode(QAbstractItemView::DragDrop); files->setDefaultDropAction(Qt::CopyAction); files->setContextMenuPolicy(Qt::CustomContextMenu); layout->addWidget(files);
        connect(categories, &QComboBox::currentIndexChanged, &dialog, [categories, proxy] { proxy->category = categories->currentData().toString(); proxy->update(); });
        connect(files, &QListView::doubleClicked, &dialog, [proxy, this](const QModelIndex& index) { QDesktopServices::openUrl(QUrl::fromLocalFile(model_->filePath(proxy->mapToSource(index)))); });
        connect(files, &QWidget::customContextMenuRequested, &dialog, [this, files](const QPoint& point) { context(files, point); });
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        const auto area = screen()->availableGeometry(); dialog.resize(qMin(dialog.width(), area.width()), qMin(dialog.height(), area.height())); dialog.exec();
    }
    Store& store_; QString id_; Notify notify_; QLineEdit* path_ = nullptr; QListView* view_ = nullptr;
    DropModel* model_ = nullptr; FileProxy* proxy_ = nullptr; QComboBox* mode_ = nullptr; QComboBox* sort_ = nullptr;
};
}
QWidget* createFolderPage(Store& store, const QString& id, const Notify& notify) { return new FolderPage(store, id, notify); }
}
