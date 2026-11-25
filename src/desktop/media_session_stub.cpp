#include <QCoreApplication>
#include "desktop/media_session.h"
namespace crossdesk::desktop {
MediaSnapshot readMediaSession(const QString&, const QString&) {
    MediaSnapshot result; result.error = QCoreApplication::translate("CrossDesk", "当前平台不支持系统媒体会话"); return result;
}
bool controlMediaSession(const QString&, const QString&, qint64, QString* error) {
    if (error) *error = QCoreApplication::translate("CrossDesk", "当前平台不支持系统媒体会话"); return false;
}
}
