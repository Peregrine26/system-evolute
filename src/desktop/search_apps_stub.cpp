#include "desktop/search_apps.h"
#include <QDesktopServices>
#include <QUrl>
namespace crossdesk::desktop {
QList<SearchHit> installedApplications() { return {}; }
bool openApplication(const QString& location) { return QDesktopServices::openUrl(QUrl::fromLocalFile(location)); }
}
