#include <QCoreApplication>
#include "desktop/appearance.h"
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QImageReader>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPixmapCache>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QSpinBox>
#include <QSysInfo>
#include <QTimer>

namespace crossdesk::desktop {
QJsonObject effectiveAppearance(const Store& store, const QString& id) {
    auto result = store.settings()["appearance"].toObject();
    if (!id.isEmpty()) {
        const auto own = store.widget(id)["appearance"].toObject();
        if (own["override"].toBool()) for (auto it = own.begin(); it != own.end(); ++it) result[it.key()] = it.value();
    }
    return result;
}
namespace {
QString color(const QJsonObject& a, const QString& key, const QString& fallback) {
    const QColor candidate(a[key].toString()); return candidate.isValid() ? candidate.name(QColor::HexArgb) : fallback;
}
}
QString appearanceStyle(const QJsonObject& a, const QString& selector) {
    auto family = a["font"].toString("Microsoft YaHei UI"); family.remove('\'').remove('"').remove(';').remove('{').remove('}');
    const int size = std::clamp(a["fontSize"].toInt(13), 9, 32);
    QString result = selector + QString(" {font-family:'%1';font-size:%2px;").arg(family).arg(size);
    if (QColor(a["foreground"].toString()).isValid()) result += "color:" + color(a, "foreground", "#273936") + ';';
    result += "}";
    return result;
}
QImage appearanceImage(const QJsonObject& a) {
    if (a["background"].toString().isEmpty()) return {};
    QImageReader reader(a["background"].toString()); reader.setAutoTransform(true);
    const auto size = reader.size(); if (!size.isValid()) return {};
    if (size.width() > 4096 || size.height() > 4096) reader.setScaledSize(size.scaled(4096, 4096, Qt::KeepAspectRatio));
    return reader.read();
}
void paintAppearance(QPainter& painter, const QRect& rect, const QJsonObject& a, const QImage& image) {
    if (QColor(a["surface"].toString()).isValid()) painter.fillRect(rect, QColor(a["surface"].toString()));
    if (image.isNull()) return;
    painter.save(); painter.setClipRect(rect); painter.setOpacity(std::clamp(a["backgroundOpacity"].toInt(30), 0, 100) / 100.0);
    const int fit = a["fit"].toInt();
    if (fit == 2) painter.drawImage(rect, image);
    else {
        const auto size = image.size().scaled(rect.size(), fit == 1 ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding);
        const QRect target(QPoint(rect.center().x() - size.width() / 2, rect.center().y() - size.height() / 2), size);
        painter.drawImage(target, image);
    }
    painter.restore();
}
bool editAppearance(Store& store, QWidget* parent, const QString& id) {
    auto appearance = effectiveAppearance(store, id);
    QDialog dialog(parent); dialog.setObjectName("appearanceDialog"); dialog.setWindowTitle(QCoreApplication::translate("CrossDesk", "字体、配色与背景"));
    auto* layout = new QFormLayout(&dialog);
    auto* override = new QCheckBox(QCoreApplication::translate("CrossDesk", "此格子使用独立外观")); override->setChecked(store.widget(id)["appearance"].toObject()["override"].toBool());
    if (!id.isEmpty()) layout->addRow(override);
    auto* font = new QFontComboBox; font->setCurrentFont(QFont(appearance["font"].toString("Microsoft YaHei UI"))); layout->addRow(QCoreApplication::translate("CrossDesk", "字体"), font);
    auto* size = new QSpinBox; size->setRange(9, 32); size->setValue(appearance["fontSize"].toInt(13)); layout->addRow(QCoreApplication::translate("CrossDesk", "文字大小"), size);
    QMap<QString, QString> colors;
    for (const auto& key : {QString("foreground"), QString("surface")}) {
        colors[key] = appearance[key].toString(); auto* picker = new QPushButton(colors[key].isEmpty() ? QCoreApplication::translate("CrossDesk", "跟随主题") : colors[key]);
        layout->addRow(key == "foreground" ? QCoreApplication::translate("CrossDesk", "文字颜色") : QCoreApplication::translate("CrossDesk", "背景颜色"), picker);
        QObject::connect(picker, &QPushButton::clicked, &dialog, [&, key, picker] {
            const auto value = QColorDialog::getColor(QColor(colors[key]), &dialog); if (!value.isValid()) return;
            colors[key] = value.name(); picker->setText(colors[key]);
        });
    }
    auto* clear = new QPushButton(QCoreApplication::translate("CrossDesk", "恢复主题配色")); layout->addRow(clear);
    QObject::connect(clear, &QPushButton::clicked, &dialog, [&] { colors.clear(); });
    auto* image = new QLineEdit(appearance["background"].toString()); layout->addRow(QCoreApplication::translate("CrossDesk", "背景图片"), image);
    auto* choose = new QPushButton(QCoreApplication::translate("CrossDesk", "选择图片")); layout->addRow(choose);
    QObject::connect(choose, &QPushButton::clicked, &dialog, [&] { const auto path = QFileDialog::getOpenFileName(&dialog, QCoreApplication::translate("CrossDesk", "背景图片"), {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp)"); if (!path.isEmpty()) image->setText(path); });
    auto* fit = new QComboBox; fit->addItems({QCoreApplication::translate("CrossDesk", "填充"), QCoreApplication::translate("CrossDesk", "适应"), QCoreApplication::translate("CrossDesk", "拉伸")}); fit->setCurrentIndex(std::clamp(appearance["fit"].toInt(), 0, 2)); layout->addRow(QCoreApplication::translate("CrossDesk", "图片适配"), fit);
    auto* alpha = new QSpinBox; alpha->setRange(0, 100); alpha->setSuffix(" %"); alpha->setValue(appearance["backgroundOpacity"].toInt(30)); layout->addRow(QCoreApplication::translate("CrossDesk", "图片透明度"), alpha);
    auto* icon = new QLineEdit(appearance["icon"].toString()); layout->addRow(QCoreApplication::translate("CrossDesk", "图标文字或图片路径"), icon);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    appearance["font"] = font->currentFont().family(); appearance["fontSize"] = size->value();
    appearance["foreground"] = colors.value("foreground"); appearance["surface"] = colors.value("surface");
    appearance["background"] = image->text(); appearance["fit"] = fit->currentIndex(); appearance["backgroundOpacity"] = alpha->value(); appearance["icon"] = icon->text();
    if (id.isEmpty()) { auto settings = store.settings(); settings["appearance"] = appearance; return store.setSettings(settings); }
    auto widget = store.widget(id); if (widget.isEmpty()) return false;
    appearance["override"] = override->isChecked(); widget["appearance"] = appearance; return store.putWidget(widget);
}
QJsonObject diagnosticSnapshot(const Store& store) {
    QJsonObject counts; for (const auto& value : store.widgets()) {
        const auto kind = value.toObject()["kind"].toString(); counts[kind] = counts[kind].toInt() + 1;
    }
    QJsonArray displays;
    for (const auto* screen : QGuiApplication::screens()) displays.append(QJsonObject{{"width", screen->size().width()}, {"height", screen->size().height()}, {"scale", screen->devicePixelRatio()}});
    // Deliberately exclude names, paths, URLs, user content, credentials and window titles.
    return {{"schema", 1}, {"application", "CrossDesk"}, {"qt", qVersion()}, {"os", QSysInfo::productType()},
        {"osVersion", QSysInfo::productVersion()}, {"architecture", QSysInfo::currentCpuArchitecture()},
        {"widgets", counts}, {"todos", store.records("todos").size()}, {"notes", store.records("notes").size()}, {"displays", displays}};
}
bool exportDiagnostics(const Store& store, const QString& path, QString* error) {
    QSaveFile output(path); const auto bytes = QJsonDocument(diagnosticSnapshot(store)).toJson();
    if (output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit()) return true;
    if (error) *error = output.errorString(); return false;
}
qint64 cacheSize(const QString& directory) {
    const QFileInfo base(directory + "/cache"); if (!base.exists() || base.isSymLink()) return 0;
    qint64 bytes = 0; QDirIterator entries(base.absoluteFilePath(), QDir::Files | QDir::NoSymLinks | QDir::Hidden, QDirIterator::Subdirectories);
    while (entries.hasNext()) { entries.next(); bytes += entries.fileInfo().size(); } return bytes;
}
bool clearManagedCache(const QString& directory, QString* error) {
    const QFileInfo base(directory + "/cache");
    if (!base.exists()) return true;
    if (base.isSymLink() || base.canonicalFilePath() != QFileInfo(directory).canonicalFilePath() + "/cache") {
        if (error) *error = QCoreApplication::translate("CrossDesk", "缓存路径异常，未清理"); return false;
    }
    bool ok = true; QDirIterator entries(base.absoluteFilePath(), QDir::Files | QDir::NoSymLinks | QDir::Hidden, QDirIterator::Subdirectories);
    while (entries.hasNext()) if (!QFile::remove(entries.next())) ok = false;
    if (!ok && error) *error = QCoreApplication::translate("CrossDesk", "部分缓存正在使用，请稍后重试"); return ok;
}
void showMaintenance(Store& store, QWidget* parent) {
    QDialog dialog(parent); dialog.setWindowTitle(QCoreApplication::translate("CrossDesk", "性能与诊断")); auto* form = new QFormLayout(&dialog);
    auto* usage = new QLabel; form->addRow(QCoreApplication::translate("CrossDesk", "可重建磁盘缓存"), usage);
    const auto update = [&] { usage->setText(QLocale().formattedDataSize(cacheSize(store.directory()))); }; update();
    auto* profile = new QComboBox; profile->addItems({QCoreApplication::translate("CrossDesk", "均衡"), QCoreApplication::translate("CrossDesk", "节省资源"), QCoreApplication::translate("CrossDesk", "自定义")}); form->addRow(QCoreApplication::translate("CrossDesk", "资源策略"), profile);
    auto* memory = new QSpinBox; memory->setRange(8, 256); memory->setSuffix(" MiB"); memory->setValue(store.settings()["cacheLimitMB"].toInt(32)); form->addRow(QCoreApplication::translate("CrossDesk", "图片缓存上限"), memory);
    auto* idle = new QSpinBox; idle->setRange(0, 3600); idle->setSuffix(QCoreApplication::translate("CrossDesk", " 秒")); idle->setValue(store.settings()["cacheIdleSeconds"].toInt(120)); form->addRow(QCoreApplication::translate("CrossDesk", "隐藏后释放图片（0 为保留）"), idle);
    auto* motion = new QCheckBox(QCoreApplication::translate("CrossDesk", "减少持续动画")); motion->setChecked(store.settings()["reducedMotion"].toBool()); form->addRow(motion);
    QObject::connect(profile, &QComboBox::currentIndexChanged, &dialog, [&](int value) { if (value < 2) { memory->setValue(value ? 8 : 32); idle->setValue(value ? 15 : 120); motion->setChecked(value == 1); } });
    profile->setCurrentIndex(std::clamp(store.settings()["resourceProfile"].toInt(2), 0, 2));
    auto* clear = new QPushButton(QCoreApplication::translate("CrossDesk", "清理可重建缓存")); form->addRow(clear);
    QObject::connect(clear, &QPushButton::clicked, &dialog, [&] { QString error; QPixmapCache::clear(); if (clearManagedCache(store.directory(), &error)) update(); else usage->setText(error); });
    auto* diagnostic = new QPushButton(QCoreApplication::translate("CrossDesk", "导出诊断信息")); form->addRow(diagnostic);
    auto* feedback = new QLabel; feedback->setWordWrap(true); form->addRow(feedback);
    QObject::connect(diagnostic, &QPushButton::clicked, &dialog, [&] {
        const auto file = QFileDialog::getSaveFileName(&dialog, QCoreApplication::translate("CrossDesk", "导出诊断信息"), "CrossDesk-diagnostics.json", "JSON (*.json)");
        if (file.isEmpty()) return; QString error; feedback->setText(exportDiagnostics(store, file, &error) ? QCoreApplication::translate("CrossDesk", "诊断已导出，不包含文件路径和记录正文") : error);
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        auto settings = store.settings(); settings["cacheLimitMB"] = memory->value(); settings["cacheIdleSeconds"] = idle->value(); settings["reducedMotion"] = motion->isChecked(); settings["resourceProfile"] = profile->currentIndex();
        if (store.setSettings(settings)) QPixmapCache::setCacheLimit(memory->value() * 1024);
    }
}
}
