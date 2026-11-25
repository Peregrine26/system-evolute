#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
namespace crossdesk::desktop {
struct MediaSessionName { QString id; QString label; };
struct MediaSnapshot {
    QList<MediaSessionName> sessions;
    QString source, title, artist, album, error;
    QByteArray cover;
    qint64 start = 0, end = 0, position = 0, minimum = 0, maximum = 0;
    bool available = false, playing = false;
    bool canToggle = false, canPrevious = false, canNext = false, canSeek = false;
    bool canShuffle = false, canRepeat = false, shuffle = false;
    int repeat = 0;
};
// All functions run on a worker thread and return value objects, never WinRT handles.
MediaSnapshot readMediaSession(const QString& selected = {}, const QString& previousTrack = {});
bool controlMediaSession(const QString& selected, const QString& action, qint64 value, QString* error);
}
