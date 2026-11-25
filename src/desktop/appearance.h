#pragma once
#include "desktop/store.h"
#include <QImage>
#include <QPainter>
namespace crossdesk::desktop {
QJsonObject effectiveAppearance(const Store&, const QString& widgetId = {});
bool editAppearance(Store&, QWidget* parent, const QString& widgetId = {});
QString appearanceStyle(const QJsonObject&, const QString& selector);
QImage appearanceImage(const QJsonObject&);
void paintAppearance(QPainter&, const QRect&, const QJsonObject&, const QImage&);
QJsonObject diagnosticSnapshot(const Store&);
bool exportDiagnostics(const Store&, const QString& file, QString* error = nullptr);
qint64 cacheSize(const QString& directory);
bool clearManagedCache(const QString& directory, QString* error = nullptr);
void showMaintenance(Store&, QWidget*);
}
