#include "platform/windows/qt_desktop_bridge.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>

#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QWidget window;
    window.resize(800, 600);
    QWidget panel(&window), region(&panel);
    panel.setGeometry(40, 50, 600, 400);
    region.setGeometry(25, 30, 200, 160);
    window.show();
    app.processEvents();
    crossdesk::QtDesktopBridge bridge(window);
    bool ok = true;
    const auto check = [&](bool condition, const char* text) {
        if (!condition) std::cerr << text << '\n';
        ok &= condition;
    };
    const auto child_id = region.internalWinId();
    QRect mapped;
    const QRect content(10, 12, 100, 60);
    check(bridge.map_preview_region(region, content, mapped).ready() &&
          mapped == QRect(75, 92, 100, 60), "nested region was not mapped to client DIPs");
    check(region.internalWinId() == child_id, "mapping created a child native window");

    region.move(80, 100);
    check(bridge.map_preview_region(region, content, mapped).ready() &&
          mapped == QRect(130, 162, 100, 60), "mapping retained the pre-animation position");
    check(bridge.map_preview_region(region, QRect(-1, 0, 20, 20), mapped).status ==
          crossdesk::DesktopStatus::InvalidRectangle && mapped.isEmpty(),
          "invalid content was accepted or retained an old rectangle");
    QWidget foreign;
    check(bridge.map_preview_region(foreign, foreign.rect(), mapped).status ==
          crossdesk::DesktopStatus::InvalidWindow, "foreign top-level region accepted");

    QWidget cover(&panel);
    cover.setGeometry(region.geometry());
    cover.show();
    cover.raise();
    app.processEvents();
    check(!bridge.map_preview_region(region, content, mapped).ready(),
          "occluded content would paint over another card");
    cover.hide();
    app.processEvents();
    auto* fade = new QGraphicsOpacityEffect(&panel);
    panel.setGraphicsEffect(fade);
    fade->setOpacity(0.25);
    check(!bridge.map_preview_region(region, content, mapped).ready(),
          "DWM bypassed a Qt ancestor fade");
    fade->setOpacity(1.0);
    check(bridge.map_preview_region(region, content, mapped).ready(),
          "completed fade did not restore preview availability");
    panel.setGraphicsEffect(nullptr);

    region.hide();
    const auto results = bridge.set_widget_previews({
        {0, nullptr, content}, {0, &region, QRect(-1, 0, 20, 20)}, {0, &region, content}
    });
    check(results.size() == 3 && results[0].status == crossdesk::DesktopStatus::InvalidWindow &&
          results[1].status == crossdesk::DesktopStatus::InvalidRectangle &&
          results[2].status == crossdesk::DesktopStatus::Unavailable,
          "per-region failure results lost their input ordering");
    region.show();
    window.hide();
    check(!bridge.map_preview_region(region, content, mapped).ready(),
          "hidden destination accepted a widget preview");
    check(!bridge.set_previews({{0, content}})[0].ready(),
          "hidden destination accepted a raw preview");
    return ok ? 0 : 1;
}
