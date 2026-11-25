#include "desktop/window_overview.h"
#include "window_layout.h"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QTreeWidget>
#include <iostream>

using namespace crossdesk;
using namespace crossdesk::desktop;
class FakeApi final : public WindowApiAdapter {
public:
    mutable std::vector<WindowInfo> windows;
    mutable int moves = 0, activations = 0;
    bool complete = true;
    WindowBounds work{100, 50, 1101, 850};
    FakeApi() {
        for (int index = 1; index <= 3; ++index) {
            WindowInfo window; window.native_handle = index; window.process_id = 10000 + index;
            window.title = index == 1 ? L"Explorer fixture" : L"CrossDesk external fixture";
            window.process_name = L"explorer.exe"; window.visible = true; window.bounds = {index * 10, 20, index * 10 + 300, 300}; windows.push_back(window);
        }
        auto own = windows[0]; own.native_handle = 99; own.process_id = std::uint32_t(QCoreApplication::applicationPid()); own.title = L"owned arbitrary title"; windows.push_back(own);
    }
    const WindowInfo* find(NativeWindowHandle handle) const { for (const auto& window : windows) if (window.native_handle == handle) return &window; return nullptr; }
    bool get_primary_work_area(WindowBounds& output) const override { output = work; return true; }
    bool enumerate_top_level_windows(const WindowCallback& callback) const override { for (const auto& window : windows) if (!callback(window.native_handle)) break; return complete; }
    std::wstring get_window_title(NativeWindowHandle w) const override { return find(w) ? find(w)->title : L""; }
    std::uint32_t get_process_id(NativeWindowHandle w) const override { return find(w) ? find(w)->process_id : 0; }
    std::wstring get_process_name(std::uint32_t) const override { return L"explorer.exe"; }
    bool get_window_bounds(NativeWindowHandle w, WindowBounds& output) const override { if (!find(w)) return false; output = find(w)->bounds; return true; }
    bool is_window_visible(NativeWindowHandle w) const override { return find(w) && find(w)->visible; }
    bool is_window_minimized(NativeWindowHandle) const override { return false; }
    WindowOperationResult hide_window(NativeWindowHandle) const override { return {}; }
    WindowOperationResult show_window_without_activation(NativeWindowHandle) const override { return {}; }
    WindowOperationResult activate_window(NativeWindowHandle) const override { ++activations; return {WindowOperationStatus::ReachedTarget, 0}; }
    WindowOperationResult set_window_bounds(NativeWindowHandle handle, const WindowBounds& bounds) const override {
        ++moves; for (auto& window : windows) if (window.native_handle == handle) window.bounds = bounds;
        return {WindowOperationStatus::ReachedTarget, 0};
    }
};
void drain(int milliseconds = 0) {
    QElapsedTimer timer; timer.start(); do { QApplication::processEvents(); if (milliseconds) QThread::msleep(2); } while (timer.elapsed() < milliseconds);
}
bool equal(const WindowBounds& a, const WindowBounds& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
int main(int argc, char** argv) {
    QApplication app(argc, argv); bool ok = true;
    auto check = [&](bool value, const char* message) { if (!value) { ok = false; std::cerr << message << '\n'; } };
    FakeApi api; auto scan = WindowsWindowScanner(api).scan_with_status();
    std::array<WindowIdentity, 2> selected{{{1, 10001}, {2, 10002}}};
    auto equalPlan = planTwoWindows(scan, selected, api.work, TwoWindowMode::Equal, std::uint32_t(app.applicationPid()));
    check(equalPlan.valid() && equal(equalPlan.plan.targets[0].bounds, {100, 50, 594, 850}) && equal(equalPlan.plan.targets[1].bounds, {606, 50, 1101, 850}), "equal layout did not preserve gap/odd-width remainder");
    auto mainPlan = planTwoWindows(scan, selected, api.work, TwoWindowMode::MainLeft, std::uint32_t(app.applicationPid()));
    check(mainPlan.valid() && equal(mainPlan.plan.targets[0].bounds, {100, 50, 693, 850}) && equal(mainPlan.plan.targets[1].bounds, {705, 50, 1101, 850}), "60:40 layout incorrect");
    check(planTwoWindows(scan, selected, {0, 0, 13, 100}, TwoWindowMode::Equal, 0).error == LayoutPlanError::InvalidWorkArea, "tiny work area accepted");
    auto duplicate = selected; duplicate[1] = duplicate[0];
    check(planTwoWindows(scan, duplicate, api.work, TwoWindowMode::Equal, 0).error == LayoutPlanError::InvalidSelection, "duplicate handle accepted");
    scan.complete = false;
    check(validateTwoWindowPlan(equalPlan.plan, scan, api.work, 0) == LayoutPlanError::ScanIncomplete, "incomplete scan not distinguished");
    scan.complete = true; scan.windows[0].process_id++;
    check(validateTwoWindowPlan(equalPlan.plan, scan, api.work, 0) == LayoutPlanError::IdentityChanged, "reused HWND identity accepted");

    WindowOverview window(api); window.showOverview(); drain();
    auto* list = window.findChild<QTreeWidget*>("windowList"); auto* preview = window.findChild<QPushButton*>("previewLayout");
    auto* confirm = window.findChild<QPushButton*>("confirmLayout"); auto* back = window.findChild<QPushButton*>("backToOverview");
    auto* ratio = window.findChild<QComboBox*>("layoutRatio"); auto* swap = window.findChild<QPushButton*>("swapLayoutWindows");
    check(list && preview && confirm && back && ratio && swap, "overview controls missing"); if (!ok) return 1;
    check(list->topLevelItemCount() == 3, "own PID filter removed external title or retained self");
    auto selectTwo = [&] { list->clearSelection(); list->topLevelItem(0)->setSelected(true); list->topLevelItem(1)->setSelected(true); drain(); };
    selectTwo(); const auto original = api.windows[0].bounds;
    preview->click(); drain();
    check(window.property("layoutMode").toString() == "preview" && api.moves == 0 && !confirm->isHidden(), "first preview action moved windows");
    ratio->setCurrentIndex(1); swap->click(); drain();
    check(api.moves == 0, "ratio or swap changed external geometry before confirm");
    back->click(); drain();
    check(window.property("layoutMode").toString() == "overview" && equal(api.windows[0].bounds, original), "back action changed source geometry");
    ratio->setCurrentIndex(0); preview->click(); drain();
    api.windows[0].bounds.left++;
    confirm->click(); drain();
    check(api.moves == 0 && window.property("layoutMode").toString() == "overview", "changed source geometry did not invalidate preview");
    selectTwo(); preview->click(); drain(); api.work.right++;
    confirm->click(); drain();
    check(api.moves == 0 && window.property("layoutMode").toString() == "overview", "changed work area did not invalidate preview");
    api.work.right--; selectTwo(); preview->click(); drain(); api.complete = false;
    confirm->click(); drain();
    check(api.moves == 0 && window.property("layoutMode").toString() == "preview", "incomplete scan moved windows or discarded safe preview");
    api.complete = true; confirm->click(); confirm->click(); drain(250);
    check(api.moves == 2 && window.property("layoutMode").toString() == "overview", "duplicate confirm bypassed submission lock");
    check(equal(api.windows[0].bounds, equalPlan.plan.targets[0].bounds) && equal(api.windows[1].bounds, equalPlan.plan.targets[1].bounds), "executed bounds differed from preview");
    list->clearSelection(); list->topLevelItem(0)->setSelected(true); list->topLevelItem(1)->setSelected(true); preview->click(); drain();
    api.windows[0].process_id++; window.showOverview(); drain();
    check(list->selectedItems().size() == 1 && api.moves == 2, "refresh retained selection on reused HWND");
    window.toggleOverview(); drain(); check(!window.isVisible(), "toggle did not hide overview");
    window.toggleOverview(); drain(); check(window.isVisible(), "toggle did not restore overview");
    bool toolsCalled = false; window.openDesktopTools = [&] { toolsCalled = true; };
    window.findChild<QPushButton*>("openDesktopTools")->click(); check(toolsCalled, "desktop tools did not use callback");
    bool closed = false; window.closeRequested = [&] { closed = true; window.hide(); }; window.close(); drain();
    check(closed && !window.isVisible(), "close did not route through shell policy");
    return ok ? 0 : 1;
}
