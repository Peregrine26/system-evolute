#include "platform/windows/qt_desktop_bridge.h"
#include "ui/preview_widgets.h"
#include "ui/window_cards.h"

#include <QApplication>

#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QWidget window, a, b;
    window.resize(900, 600);
    a.resize(320, 200);
    b.resize(320, 200);
    crossdesk::ui::WindowCard card(&window);
    crossdesk::ui::ThumbnailSlot pane(&window);
    card.setGeometry(20, 20, 300, 300);
    pane.setGeometry(400, 20, 400, 400);
    crossdesk::ui::WindowItem first, second;
    first.id = static_cast<quintptr>(a.winId());
    first.title = QStringLiteral("Own source A");
    second.id = static_cast<quintptr>(b.winId());
    second.title = QStringLiteral("Own source B");
    card.setWindow(first);
    pane.setWindow(&second);
    a.show(); b.show(); window.show(); app.processEvents();
    const auto card_child = card.internalWinId();
    const auto pane_child = pane.internalWinId();
    const auto root = static_cast<quintptr>(window.winId());
    if (card.hostHandle() != root || pane.hostHandle() != root ||
        card.internalWinId() != card_child || pane.internalWinId() != pane_child) return 1;
    crossdesk::QtDesktopBridge bridge(window);
    const auto results = bridge.set_previews({{first.id, card.previewRect()}, {second.id, pane.previewRect()}});
    if (results.size() != 2 || !results[0].ready() || !results[1].ready()) {
        std::cerr << "C preview interfaces did not produce valid top-level DWM destinations\n";
        return 1;
    }
    return 0;
}
