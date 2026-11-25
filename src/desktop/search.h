#pragma once
#include <QList>
#include <QString>
#include <QDateTime>
namespace crossdesk::desktop {
struct SearchHit { QString name, kind, location, id; QDateTime modified; qint64 size = 0; };
bool matchesSearch(const SearchHit&, const QString& query);
QList<SearchHit> systemSearchEntries();
}
