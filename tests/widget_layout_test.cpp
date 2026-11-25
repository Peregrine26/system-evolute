#include "desktop/widget_layout.h"

#include <QCoreApplication>
#include <iostream>

using namespace crossdesk::desktop;
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    bool ok = true;
    auto check = [&](bool value, const char* description) {
        if (!value) { ok = false; std::cerr << description << '\n'; }
    };
    const WidgetScreen left{"hardware-left", QRect(-1920, -80, 1920, 1040)};
    const WidgetScreen primary{"hardware-main", QRect(0, 0, 2560, 1400)};
    const auto home = rememberUserPlacement({{"kind", "todo"}}, QRect(-1600, 120, 480, 400), left);
    auto restored = restoreWidgetPlacement(home, {left, primary}, primary.key);
    check(restored.valid && !restored.fallback && restored.screen == left.key && restored.geometry == QRect(-1600, 120, 480, 400), "negative-origin home was not preserved");

    const auto fallback = restoreWidgetPlacement(home, {primary}, primary.key);
    check(fallback.valid && fallback.fallback && fallback.screen == primary.key && primary.workArea.contains(fallback.geometry), "unplugged-screen fallback escaped work area");
    check(fallback.geometry.width() == 640 && fallback.geometry.height() == 538, "lost-screen size was not proportionally remapped");
    auto adapted = applyRestoredPlacement(home, fallback);
    check(adapted["layoutHome"] == home["layoutHome"] && adapted["layoutFallback"].toBool(), "automatic fallback overwrote preferred home");
    restored = restoreWidgetPlacement(adapted, {primary, left}, primary.key);
    check(!restored.fallback && restored.screen == left.key && restored.geometry == QRect(-1600, 120, 480, 400), "reconnected screen did not restore original home");

    auto moved = rememberUserPlacement(adapted, QRect(700, 600, 500, 400), primary);
    restored = restoreWidgetPlacement(moved, {left, primary}, primary.key);
    check(!restored.fallback && restored.screen == primary.key && restored.geometry == QRect(700, 600, 500, 400), "deliberate fallback move did not adopt new home");
    check(!moved["layoutFallback"].toBool(), "deliberate placement retained fallback marker");

    const WidgetScreen reorderedLeft{left.key, QRect(2560, 0, 1920, 1040)};
    restored = restoreWidgetPlacement(home, {primary, reorderedLeft}, primary.key);
    check(restored.geometry == QRect(2880, 200, 480, 400), "screen reordering did not preserve relative manual position and size");
    const WidgetScreen smallerLeft{left.key, QRect(-1280, 0, 1280, 720)};
    restored = restoreWidgetPlacement(home, {smallerLeft, primary}, primary.key);
    check(restored.geometry.width() == 480 && restored.geometry.height() == 400 && smallerLeft.workArea.contains(restored.geometry), "same-screen resize scaled or stranded widget");
    const auto temporary = applyRestoredPlacement(home, restored);
    check(restoreWidgetPlacement(temporary, {left, primary}, primary.key).geometry == QRect(-1600, 120, 480, 400), "temporary resolution clamp destroyed desired placement");

    const WidgetScreen tiny{"tiny", QRect(-40, 30, 100, 80)};
    restored = restoreWidgetPlacement(home, {tiny}, tiny.key);
    check(restored.geometry == tiny.workArea, "tiny work area did not contain widget");
    const auto tinyConfig = applyRestoredPlacement(home, restored);
    check(tinyConfig["width"].toInt() >= 240 && tinyConfig["height"].toInt() >= 150, "tiny work area wrote invalid persistent dimensions");

    const QJsonObject legacy{{"screen", "disconnected-legacy"}, {"x", -3000}, {"y", -2000}, {"width", 360}, {"height", 320}};
    restored = restoreWidgetPlacement(legacy, {primary}, primary.key);
    check(restored.fallback && restored.geometry == QRect(0, 0, 360, 320), "legacy offscreen coordinates were not safely clamped");
    check(restored.home["screen"].toString() == "disconnected-legacy", "legacy missing-screen identity lost");
    const auto original = legacy;
    restoreWidgetPlacement(legacy, {primary}, primary.key);
    check(legacy == original, "resolver mutated input");
    check(!restoreWidgetPlacement(home, {}, {}).valid, "empty display list returned usable placement");
    check(!restoreWidgetPlacement(home, {{"bad", {0, 0, 0, 0}}}, "bad").valid, "invalid work area accepted");

    auto invalidHome = legacy; invalidHome["layoutHome"] = QJsonObject{{"screen", "other"}, {"width", -50}, {"height", 600}};
    check(restoreWidgetPlacement(invalidHome, {primary}, primary.key).geometry == QRect(0, 0, 360, 320), "malformed home displaced valid legacy geometry");
    const auto initial = restoreWidgetPlacement({{"x", 80}, {"y", 100}, {"width", 360}, {"height", 320}}, {primary}, primary.key);
    check(!initial.fallback && initial.home["screen"].toString() == primary.key, "new widget did not adopt first screen");
    auto knownLegacy = legacy; knownLegacy["screen"] = left.key; knownLegacy["x"] = -1700; knownLegacy["y"] = 100;
    const auto migrated = restoreWidgetPlacement(knownLegacy, {left, primary}, primary.key);
    check(migrated.home["work"].toObject()["x"].toInt() == -1920 && migrated.home["work"].toObject()["width"].toInt() == 1920, "legacy home did not learn its available work area");
    const auto migratedFallback = restoreWidgetPlacement(applyRestoredPlacement(knownLegacy, migrated), {primary}, primary.key);
    check(migratedFallback.fallback && migratedFallback.geometry.width() == 480, "migrated legacy home could not proportionally fall back");
    const auto extreme = restoreWidgetPlacement({{"screen", primary.key}, {"x", 2147483647}, {"y", -2147483647}, {"width", 16000}, {"height", 16000}}, {primary}, primary.key);
    check(extreme.valid && primary.workArea.contains(extreme.geometry), "extreme stored coordinates escaped integer-safe clamp");
    check(rememberUserPlacement(home, {0, 0, 0, 0}, primary) == home, "invalid user geometry replaced home");
    const auto oversized = rememberUserPlacement({}, QRect(0, 0, 19000, 18000), {"huge", QRect(0, 0, 20000, 20000)});
    check(oversized["width"].toInt() == 16000 && oversized["height"].toInt() == 16000, "manual placement exceeded persisted schema extent limits");
    return ok ? 0 : 1;
}
