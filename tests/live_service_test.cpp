#include "ui/live_service.h"
#include "ui/main_window.h"
#include "platform/window_api_adapter.h"

#include <QApplication>
#include <QLabel>
#include <QShortcut>

#include <iostream>

class FakeApi : public crossdesk::WindowApiAdapter {
public:
    bool complete = true;
    bool enumerate_top_level_windows(const WindowCallback& callback) const override {
        for (std::uintptr_t i = 1; i <= 12; ++i) {
            if (!callback(i)) break;
            if (!complete && i == 3) return false;
        }
        return complete;
    }
    std::wstring get_window_title(std::uintptr_t id) const override { return L"Window " + std::to_wstring(id); }
    std::uint32_t get_process_id(std::uintptr_t) const override { return 0xfffffffeu; }
    std::wstring get_process_name(std::uint32_t) const override { return L"fixture.exe"; }
    bool get_window_bounds(std::uintptr_t id, crossdesk::WindowBounds& box) const override {
        box = {0, 0, static_cast<int>(1000 - id), 600}; return true;
    }
    bool is_window_visible(std::uintptr_t) const override { return true; }
    bool is_window_minimized(std::uintptr_t) const override { return false; }
    crossdesk::WindowOperationResult hide_window(std::uintptr_t) const override { return {}; }
    crossdesk::WindowOperationResult show_window_without_activation(std::uintptr_t) const override { return {}; }
    crossdesk::WindowOperationResult set_window_bounds(std::uintptr_t, const crossdesk::WindowBounds&) const override { return {}; }
    crossdesk::WindowOperationResult activate_window(std::uintptr_t) const override { return {}; }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    auto api = std::make_unique<FakeApi>();
    auto* fake = api.get();
    crossdesk::ui::LiveService service(std::move(api));
    int changed = 0;
    bool failure = false;
    QObject::connect(&service, &crossdesk::ui::CrossDeskService::windowsChanged, [&] { ++changed; });
    QObject::connect(&service, &crossdesk::ui::CrossDeskService::operationFinished,
                     [&](const crossdesk::ui::OperationResult& result) { failure = !result.success; });
    service.refresh();
    if (service.windows().size() != 12 || service.windows().last().id != 12 || changed != 1) {
        std::cerr << "valid candidates after the tenth were lost\n"; return 1;
    }
    fake->complete = false;
    service.refresh();
    if (service.windows().size() != 12 || changed != 1 || !failure) {
        std::cerr << "partial scan replaced the complete snapshot\n"; return 1;
    }
    crossdesk::ui::MainWindow window(&service);
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusLabel"));
    if (!status || !status->text().contains(QStringLiteral("扫描失败"))) {
        std::cerr << "startup hid the scan failure\n"; return 1;
    }
    bool invoked = false;
    for (auto* shortcut : window.findChildren<QShortcut*>()) {
        if (shortcut->key() == QKeySequence(QStringLiteral("Ctrl+R"))) {
            invoked = QMetaObject::invokeMethod(shortcut, "activated");
        }
    }
    if (!invoked || !status->text().contains(QStringLiteral("扫描失败"))) {
        std::cerr << "refresh success text overwrote the scan failure\n"; return 1;
    }
    return 0;
}
