// Optional compatibility fixture: compile against C's unchanged PR checkout.
#include "platform/windows/qt_desktop_bridge.h"
#include "ui/main_window.h"
#include "ui/preview_widgets.h"
#include "ui/window_cards.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QThread>

#include <iostream>

namespace {
class OwnWindowService : public crossdesk::ui::CrossDeskService {
public:
    QVector<crossdesk::ui::WindowItem> items;
    QVector<crossdesk::ui::WindowItem> windows() const override { return items; }
    QRect workArea() const override { return {0, 0, 1280, 720}; }
    void refresh() override { emit windowsChanged(); }
    void activateWindow(crossdesk::ui::WindowId) override {}
    void placeWindow(crossdesk::ui::WindowId, const QRect&) override {}
};
void pump(int milliseconds) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < milliseconds) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const bool desktop = app.arguments().contains(QStringLiteral("--desktop"));
    QWidget source_a, source_b;
    source_a.resize(320, 200);
    source_b.resize(320, 200);
    source_a.show();
    source_b.show();
    OwnWindowService service;
    for (auto* source : {&source_a, &source_b}) {
        crossdesk::ui::WindowItem item;
        item.id = static_cast<quintptr>(source->winId());
        item.title = QStringLiteral("A-owned compatibility source");
        item.processName = QStringLiteral("crossdesk selftest");
        item.bounds = QRect(0, 0, 320, 200);
        item.hasBounds = true;
        service.items.push_back(item);
    }
    crossdesk::ui::MainWindow window(&service);
    window.resize(1200, 800);
    window.show();
    crossdesk::QtDesktopBridge bridge(window);
    bridge.hotkey_state_changed = [&](crossdesk::DesktopResult state) {
        window.setAllowHide(state.ready());
    };
    // No global registration in this fixture. The default remains taskbar fallback.
    bridge.unregister_hotkey();
    pump(250);  // C's startup/selection fades must complete before DWM is shown.
    auto* cards = window.findChild<crossdesk::ui::CardStack*>(QStringLiteral("cardStack"));
    const auto panes = window.findChildren<crossdesk::ui::ThumbnailSlot*>();
    if (!cards || cards->cardCount() != 2 || panes.isEmpty()) return 1;
    auto* card = cards->cardAt(0);
    const auto before = card->internalWinId();
    std::vector<crossdesk::QtWidgetPreview> requests;
    for (int i = 0; i < cards->cardCount(); ++i) {
        auto* current = cards->cardAt(i);
        // C's painted card: preserve 12px border, 46px title/process band and 8px gap.
        requests.push_back({current->windowId(), current,
            QRect(12, 12, current->width() - 24, current->height() - 66)});
    }
    for (auto* pane : panes) {
        if (pane->isVisible()) {
            // Preserve title decoration and the floating status message at the bottom.
            requests.push_back({pane->windowId(), pane,
                pane->rect().adjusted(30, 50, -30, -70)});
        }
    }
    bool ok = requests.size() == 3;
    for (const auto& request : requests) {
        QRect mapped;
        const auto result = bridge.map_preview_region(*request.widget, request.content, mapped);
        if (!result.ready()) {
            std::cerr << "C widget content mapping failed: " << int(result.status) << '\n';
            ok = false;
        }
    }
    if (desktop) {
        for (const auto& result : bridge.set_widget_previews(requests)) {
            if (!result.ready()) {
                std::cerr << "C widget DWM registration failed: " << result.platform_error << '\n';
                ok = false;
            }
        }
    }
    ok &= card->internalWinId() == before;
    bridge.clear_previews();
    window.hide();
    QRect mapped;
    ok &= !bridge.map_preview_region(*card, requests[0].content, mapped).ready();
    std::cout << "C card/selected pane regions=" << requests.size()
              << " desktop=" << desktop << " success=" << ok << '\n';
    return ok ? 0 : 1;
}
