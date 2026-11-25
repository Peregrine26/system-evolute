#pragma once

#include <QString>
#include <QStringList>
#include <QPoint>
#include <QDate>
#include <cstdint>

namespace crossdesk::desktop {
bool systemFileMenu(const QString& path, std::uintptr_t owner, const QPoint& position);
bool fileProperties(const QString& path, std::uintptr_t owner);
bool openWith(const QString& path, std::uintptr_t owner);
bool mediaKey(const QString& command);
int systemVolume();
bool setSystemVolume(int value);
bool setAutoStart(bool enabled, const QString& executable, QString* error);
QString lunarDate(const QDate& date);
// 1/-1 denote the start/end of this window's native move/resize gesture.
int windowGeometryGesture(void* nativeMessage);
}
