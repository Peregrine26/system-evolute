#include <QCoreApplication>
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

#include "desktop/page_factories.h"
namespace crossdesk::desktop {
namespace { using Notify = std::function<void(const QString&)>; }
void organizeFolder(Store& store, const QString& folder, QWidget* parent, const Notify& notify) {
    const auto files = QDir(folder).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    if (!QFileInfo(folder).isDir() || files.isEmpty()) { notify(QCoreApplication::translate("CrossDesk", "没有可整理的文件")); return; }
    QDialog dialog(parent); dialog.setObjectName("organizationPreview"); dialog.setWindowTitle(QCoreApplication::translate("CrossDesk", "整理预览")); dialog.resize(720, 440);
    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(files.size(), 2); table->setHorizontalHeaderLabels({QCoreApplication::translate("CrossDesk", "原文件"), QCoreApplication::translate("CrossDesk", "目标目录")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); table->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table);
    const QStringList categories{QCoreApplication::translate("CrossDesk", "图片"), QCoreApplication::translate("CrossDesk", "文档"), QCoreApplication::translate("CrossDesk", "影音"), QCoreApplication::translate("CrossDesk", "压缩包"), QCoreApplication::translate("CrossDesk", "快捷方式"), QCoreApplication::translate("CrossDesk", "其他")};
    for (int i = 0; i < files.size(); ++i) {
        auto* name = new QTableWidgetItem(files[i].fileName()); name->setCheckState(Qt::Checked); table->setItem(i, 0, name);
        auto* target = new QComboBox;
        for (const auto& category : categories) target->addItem(category, folder + '/' + category);
        target->setCurrentText(categoryFor(files[i].absoluteFilePath()));
        for (const auto& value : store.widgets()) {
            const auto widget = value.toObject(); if (widget["kind"].toString() != "folder") continue;
            const auto path = widget["data"].toObject()["path"].toString();
            target->addItem(QCoreApplication::translate("CrossDesk", "现有格子 · ") + widget["title"].toString(), path);
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
        if (directory.isEmpty() || !QDir().mkpath(directory)) { errors << files[i].fileName() + QCoreApplication::translate("CrossDesk", ": 无法创建分类目录"); continue; }
        const auto result = movePath(files[i].absoluteFilePath(), directory + '/' + files[i].fileName());
        if (result.ok) { ++success; recordMove(store, files[i].absoluteFilePath(), result.destination); }
        else errors << files[i].fileName() + ": " + result.error;
    }
    const auto message = QCoreApplication::translate("CrossDesk", "已整理 %1 项，失败 %2 项").arg(success).arg(errors.size()); notify(message);
    if (!errors.isEmpty()) QMessageBox::warning(parent, QCoreApplication::translate("CrossDesk", "部分项目未移动"), message + '\n' + errors.join('\n'));
}
QString kindTitle(const QString& kind) {
    const QMap<QString, QString> names{{"folder", QCoreApplication::translate("CrossDesk", "文件格子")}, {"todo", QCoreApplication::translate("CrossDesk", "待办")},
        {"note", QCoreApplication::translate("CrossDesk", "随记")}, {"clock", QCoreApplication::translate("CrossDesk", "时光")}, {"weather", QCoreApplication::translate("CrossDesk", "天气")},
        {"music", QCoreApplication::translate("CrossDesk", "音乐")}, {"search", QCoreApplication::translate("CrossDesk", "搜索")}};
    return names.value(kind, kind);
}
void flushPages(QWidget* root) { flushRecordPages(root); }
QWidget* createPage(Store& store, const QString& id, const Notify& notify) {
    const auto kind = store.widget(id)["kind"].toString();
    if (kind == "folder") return createFolderPage(store, id, notify);
    if (kind == "todo" || kind == "note") return createRecordPage(store, kind, notify);
    if (kind == "search") return createSearchPage(store, notify);
    return createUtilityPage(store, id, notify);
}
} // namespace crossdesk::desktop
