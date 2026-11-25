#pragma once

#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>

namespace crossdesk::desktop {

// All coordinates are Qt logical desktop coordinates. This model is only for
// CrossDesk-owned widgets; external-window layout uses the platform API units.
struct WidgetScreen {
    QString key;
    QRect workArea;
};

struct WidgetPlacement {
    QRect geometry;
    QString screen;
    bool fallback = false;
    QJsonObject home;
    bool valid = false;
};

WidgetPlacement restoreWidgetPlacement(const QJsonObject& config,
                                      const QList<WidgetScreen>& screens,
                                      const QString& primaryScreen);

// Automatic adaptation records the visible placement without adopting it as
// the preferred home. A reconnected home screen can therefore restore it.
QJsonObject applyRestoredPlacement(const QJsonObject& config,
                                  const WidgetPlacement& placement);

// Call only for deliberate user move/resize (native title-bar drag or an
// explicit placement command), not construction, collapse or screen changes.
QJsonObject rememberUserPlacement(const QJsonObject& config,
                                 const QRect& geometry,
                                 const WidgetScreen& screen);

} // namespace crossdesk::desktop
