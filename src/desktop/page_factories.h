#pragma once
#include "desktop/pages.h"
namespace crossdesk::desktop {
QWidget* createFolderPage(Store&, const QString& id, const std::function<void(const QString&)>&);
QWidget* createRecordPage(Store&, const QString& kind, const std::function<void(const QString&)>&);
void flushRecordPages(QWidget*);
QWidget* createSearchPage(Store&, const std::function<void(const QString&)>&);
QWidget* createUtilityPage(Store&, const QString& id, const std::function<void(const QString&)>&);
}
