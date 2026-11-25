#include <QCoreApplication>
#include "desktop/shell.h"
namespace crossdesk::desktop {
bool systemFileMenu(const QString&, std::uintptr_t, const QPoint&) { return false; }
bool fileProperties(const QString&, std::uintptr_t) { return false; }
bool openWith(const QString&, std::uintptr_t) { return false; }
bool mediaKey(const QString&) { return false; }
int systemVolume() { return -1; }
bool setSystemVolume(int) { return false; }
bool setAutoStart(bool, const QString&, QString* error) {
    if (error) *error = QCoreApplication::translate("CrossDesk", "当前平台暂不支持开机启动");
    return false;
}
QString lunarDate(const QDate&) { return {}; }
}
