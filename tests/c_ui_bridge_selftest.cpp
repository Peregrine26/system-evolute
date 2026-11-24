// Optional compatibility fixture: compile against C's unchanged PR checkout.
#include "platform/windows/qt_desktop_bridge.h"
#include "ui/main_window.h"
#include "ui/preview_widgets.h"
#include "ui/window_cards.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QThread>

#include <cstddef>
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

// The bridge rejects regions under a fading ancestor and reports
// DesktopStatus::Unavailable until the fade ends. C animates its startup and
// selection fades, so a single mapping right after a fixed delay is a race: a
// loaded CI runner finishes the fade later than a developer machine. Wait for
// a stable state within a bounded budget instead of guessing the duration.
struct MappingOutcome {
    std::vector<crossdesk::DesktopResult> results;
    bool ready() const {
        for (const auto& result : results) {
            if (!result.ready()) return false;
        }
        return true;
    }
};

MappingOutcome map_all_regions(const crossdesk::QtDesktopBridge& bridge,
                               const std::vector<crossdesk::QtWidgetPreview>& requests,
                               int timeout_ms) {
    QElapsedTimer elapsed;
    elapsed.start();
    MappingOutcome outcome;
    for (;;) {
        outcome.results.clear();
        for (const auto& request : requests) {
            QRect mapped;
            outcome.results.push_back(
                bridge.map_preview_region(*request.widget, request.content, mapped));
        }
        if (outcome.ready() || elapsed.elapsed() >= timeout_ms) return outcome;
        pump(5);
    }
}

void report_mapping_failure(std::size_t index, const crossdesk::QtWidgetPreview& request,
                            const crossdesk::DesktopResult& result) {
    std::cerr << "C widget content mapping failed: region=" << index
              << " status=" << int(result.status);
    if (request.widget) {
        std::cerr << " widget=" << request.widget->metaObject()->className()
                  << ' ' << request.widget->width() << 'x' << request.widget->height()
                  << " visible=" << request.widget->isVisible();
    }
    std::cerr << " content=" << request.content.x() << ',' << request.content.y()
              << ' ' << request.content.width() << 'x' << request.content.height()
              << '\n';
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
    if (!cards || cards->cardCount() != 2 || panes.isEmpty()) {
        std::cerr << "C fixture layout changed: cards=" << (cards ? cards->cardCount() : -1)
                  << " panes=" << panes.size() << '\n';
        return 1;
    }
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
    // Two cards plus C's currently selected pane. Update this expectation
    // together with C's card count and default selection.
    constexpr std::size_t kExpectedRegions = 3;
    bool ok = requests.size() == kExpectedRegions;
    if (!ok) {
        std::cerr << "C region count mismatch: got " << requests.size()
                  << " expected " << kExpectedRegions << '\n';
    }
    const auto mapping = map_all_regions(bridge, requests, 10000);
    for (std::size_t i = 0; i < mapping.results.size(); ++i) {
        if (mapping.results[i].ready()) continue;
        report_mapping_failure(i, requests[i], mapping.results[i]);
        ok = false;
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
