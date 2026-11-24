#pragma once
#include "desktop/store.h"
#include <QWidget>
#include <functional>
namespace crossdesk::desktop {
QWidget* createPage(Store& store, const QString& id,
                    const std::function<void(const QString&)>& notify);
QString kindTitle(const QString& kind);
void flushPages(QWidget* root);
void organizeFolder(Store& store, const QString& path, QWidget* parent,
                    const std::function<void(const QString&)>& notify);
}
