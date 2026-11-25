#include "desktop/widget_layout.h"

#include <QtMath>
#include <algorithm>
#include <limits>

namespace crossdesk::desktop {
namespace {
constexpr int minimumWidth = 240;
constexpr int minimumHeight = 150;
constexpr int maximumExtent = 16000;

bool usable(const QRect& rect) {
    return rect.width() > 0 && rect.height() > 0 &&
        qint64(rect.x()) + rect.width() - 1 <= std::numeric_limits<int>::max() &&
        qint64(rect.y()) + rect.height() - 1 <= std::numeric_limits<int>::max();
}

QJsonObject rectangle(const QRect& rect) {
    return {{"x", rect.x()}, {"y", rect.y()},
            {"width", rect.width()}, {"height", rect.height()}};
}

QRect readRectangle(const QJsonObject& object) {
    const int x = object["x"].toInt(), y = object["y"].toInt();
    const int width = object["width"].toInt(), height = object["height"].toInt();
    if (width <= 0 || height <= 0 ||
        qint64(x) + width - 1 > std::numeric_limits<int>::max() ||
        qint64(y) + height - 1 > std::numeric_limits<int>::max()) return {};
    return {x, y, width, height};
}

bool hasHome(const QJsonObject& home) {
    return home["screen"].isString() && !home["screen"].toString().isEmpty() &&
        home["x"].isDouble() && home["y"].isDouble() &&
        home["width"].toInt() >= 180 && home["height"].toInt() >= 120 &&
        home["width"].toInt() <= maximumExtent && home["height"].toInt() <= maximumExtent;
}

QRect desiredGeometry(const QJsonObject& record) {
    const int width = std::clamp(record["width"].toInt(360), minimumWidth, maximumExtent);
    const int height = std::clamp(record["height"].toInt(320), minimumHeight, maximumExtent);
    const int x = qMin(record["x"].toInt(80), std::numeric_limits<int>::max() - width + 1);
    const int y = qMin(record["y"].toInt(100), std::numeric_limits<int>::max() - height + 1);
    return {x, y, width, height};
}

QRect contained(qint64 x, qint64 y, qint64 width, qint64 height, const QRect& area) {
    const int w = int(std::clamp(width, qint64(qMin(minimumWidth, area.width())), qint64(qMin(maximumExtent, area.width()))));
    const int h = int(std::clamp(height, qint64(qMin(minimumHeight, area.height())), qint64(qMin(maximumExtent, area.height()))));
    const auto left = std::clamp(x, qint64(area.x()), qint64(area.x()) + area.width() - w);
    const auto top = std::clamp(y, qint64(area.y()), qint64(area.y()) + area.height() - h);
    return {int(left), int(top), w, h};
}

QJsonObject homeFor(const QRect& geometry, const WidgetScreen& screen) {
    auto home = rectangle(geometry); home["screen"] = screen.key;
    home["work"] = rectangle(screen.workArea); return home;
}
}

WidgetPlacement restoreWidgetPlacement(const QJsonObject& config,
                                      const QList<WidgetScreen>& screens,
                                      const QString& primaryScreen) {
    WidgetPlacement result;
    QList<WidgetScreen> available;
    for (const auto& screen : screens) if (!screen.key.isEmpty() && usable(screen.workArea)) available << screen;
    if (available.isEmpty()) return result;

    auto home = config["layoutHome"].toObject();
    if (!hasHome(home)) {
        home = rectangle(desiredGeometry(config)); home["screen"] = config["screen"].toString();
        // Legacy records did not store a work area. Keep their absolute desired
        // position; do not invent a scale from another monitor's dimensions.
    }
    const QString preferred = home["screen"].toString();
    const WidgetScreen* target = nullptr;
    for (const auto& screen : available) if (screen.key == preferred) { target = &screen; break; }
    const bool missingHome = !preferred.isEmpty() && !target;
    if (!target) {
        for (const auto& screen : available) if (screen.key == primaryScreen) { target = &screen; break; }
        if (!target) target = &available.first();
    }

    const auto desired = desiredGeometry(home);
    const auto oldArea = readRectangle(home["work"].toObject());
    qint64 x = desired.x(), y = desired.y(), width = desired.width(), height = desired.height();
    if (usable(oldArea)) {
        if (missingHome) {
            // A lost display temporarily maps the widget proportionally to the
            // remaining work area. The untouched home retains the original size.
            x = qint64(target->workArea.x()) + qRound64((double(desired.x()) - oldArea.x()) * target->workArea.width() / oldArea.width());
            y = qint64(target->workArea.y()) + qRound64((double(desired.y()) - oldArea.y()) * target->workArea.height() / oldArea.height());
            width = qRound64(double(desired.width()) * target->workArea.width() / oldArea.width());
            height = qRound64(double(desired.height()) * target->workArea.height() / oldArea.height());
        } else {
            // Reordering or changing the primary display translates the screen's
            // logical origin, without scaling a manually placed widget.
            x += qint64(target->workArea.x()) - oldArea.x();
            y += qint64(target->workArea.y()) - oldArea.y();
        }
    }
    result.geometry = contained(x, y, width, height, target->workArea);
    result.screen = target->key; result.fallback = missingHome; result.valid = true;
    if (preferred.isEmpty()) home = homeFor(desired, *target);
    else if (!missingHome && !usable(oldArea)) home["work"] = rectangle(target->workArea);
    result.home = home;
    return result;
}

QJsonObject applyRestoredPlacement(const QJsonObject& config,
                                  const WidgetPlacement& placement) {
    if (!placement.valid) return config;
    auto next = config;
    next["x"] = placement.geometry.x(); next["y"] = placement.geometry.y();
    // Persisted dimensions stay within the existing schema even on an unusually
    // small virtual work area. The resolver still returns a contained live rect.
    next["width"] = qMax(minimumWidth, placement.geometry.width());
    next["height"] = qMax(minimumHeight, placement.geometry.height());
    next["screen"] = placement.screen; next["layoutHome"] = placement.home;
    next["layoutFallback"] = placement.fallback;
    return next;
}

QJsonObject rememberUserPlacement(const QJsonObject& config,
                                 const QRect& geometry,
                                 const WidgetScreen& screen) {
    if (screen.key.isEmpty() || !usable(screen.workArea) || !usable(geometry)) return config;
    WidgetPlacement placement;
    placement.geometry = contained(geometry.x(), geometry.y(), geometry.width(), geometry.height(), screen.workArea);
    placement.screen = screen.key; placement.home = homeFor(placement.geometry, screen); placement.valid = true;
    return applyRestoredPlacement(config, placement);
}

} // namespace crossdesk::desktop
