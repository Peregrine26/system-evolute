#pragma once
#include "desktop/store.h"
#include <QStringList>

namespace crossdesk::desktop {
QJsonObject findRecord(const Store& store, const QString& kind, const QString& id);
bool patchRecord(Store& store, const QString& kind, const QString& id, const QJsonObject& fields);
bool removeRecords(Store& store, const QString& kind, const QStringList& ids);
bool completeTasks(Store& store, const QStringList& ids, bool done,
                   const QDateTime& now = QDateTime::currentDateTime());
QStringList recordAttachmentPaths(const QJsonObject& record);
bool addRecordAttachments(Store& store, const QString& kind, const QString& id,
                          const QStringList& paths, bool copyToManaged, QString* error = nullptr);
bool removeRecordAttachments(Store& store, const QString& kind, const QString& id,
                             const QStringList& paths, bool deleteManagedCopy, QString* error = nullptr);
}
