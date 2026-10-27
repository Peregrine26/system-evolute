// Only self-owned hidden windows; no focus changes or user desktop targets.
#include "ui/live_service.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <windows.h>
#include <atomic>
#include <functional>
#include <future>
#include <iostream>
#include <thread>

namespace {
struct Behavior {
    std::atomic<int> delay{0};
    std::atomic<bool> entered{false};
    std::atomic<bool> reject{false};
    std::atomic<bool> close{false};
};
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* behavior = reinterpret_cast<Behavior*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (behavior && message == WM_WINDOWPOSCHANGING) {
        const int delay = behavior->delay.exchange(0);
        if (delay) { behavior->entered = true; Sleep(static_cast<DWORD>(delay)); }
        if (behavior->reject) {
            reinterpret_cast<WINDOWPOS*>(lp)->flags |= SWP_NOMOVE | SWP_NOSIZE;
        }
    }
    if (behavior && message == WM_WINDOWPOSCHANGED && behavior->close.exchange(false)) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}
struct Fixture {
    Behavior behavior;
    HWND window = nullptr;
    std::thread thread;
    Fixture() {
        std::promise<HWND> ready;
        auto future = ready.get_future();
        thread = std::thread([this, &ready] {
            WNDCLASSW cls{};
            const auto name = L"CrossDeskAsyncCommandsFixture" + std::to_wstring(GetCurrentThreadId());
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpfnWndProc = procedure;
            cls.lpszClassName = name.c_str();
            if (!RegisterClassW(&cls)) { ready.set_value(nullptr); return; }
            const HWND own = CreateWindowW(cls.lpszClassName, L"Own hidden fixture",
                WS_OVERLAPPEDWINDOW, 0, 0, 320, 200, nullptr, nullptr, cls.hInstance, &behavior);
            ready.set_value(own);
            if (own) {
                MSG msg{};
                while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
            }
            UnregisterClassW(cls.lpszClassName, cls.hInstance);
        });
        window = future.get();
    }
    ~Fixture() {
        if (IsWindow(window)) PostMessageW(window, WM_CLOSE, 0, 0);
        if (thread.joinable()) thread.join();
    }
    crossdesk::ui::WindowId id() const { return reinterpret_cast<std::uintptr_t>(window); }
};
bool until(const std::function<bool()>& done, int timeout = 4000) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    return done();
}
bool matches(HWND window, const QRect& target) {
    RECT rect{};
    return GetWindowRect(window, &rect) && rect.left == target.x() && rect.top == target.y()
        && rect.right - rect.left == target.width() && rect.bottom - rect.top == target.height();
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failures; std::cerr << message << '\n'; }
    };
    Fixture fixture;
    if (!fixture.window) return 2;
    crossdesk::ui::LiveService service;
    QVector<crossdesk::ui::OperationResult> results;
    QObject::connect(&service, &crossdesk::ui::CrossDeskService::operationFinished,
                     [&](const auto& result) { results.push_back(result); });
    int ticks = 0;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
    heartbeat.start(10);

    fixture.behavior.delay = 700;
    const QRect target(10, 20, 420, 310);
    QElapsedTimer elapsed;
    elapsed.start();
    service.placeWindow(fixture.id(), target);
    check(elapsed.elapsed() < 100, "placeWindow blocked its caller");
    check(results.isEmpty(), "request acceptance was reported as success");
    service.placeWindow(fixture.id() + 1, target);
    check(results.size() == 1 && !results[0].success && results[0].window == fixture.id() + 1,
          "busy rejection lost the requested identity");
    check(until([&] { return fixture.behavior.entered.load(); }), "slow fixture was not reached");
    check(results.size() == 1, "unapplied bounds were reported as complete");
    check(until([&] { return results.size() == 2; }) && results.last().success
          && matches(fixture.window, target) && ticks > 15, "busy window froze UI or failed verification");

    results.clear();
    fixture.behavior.reject = true;
    service.placeWindow(fixture.id(), QRect(30, 40, 430, 320));
    check(until([&] { return !results.isEmpty(); }) && !results.last().success,
          "ignored placement was reported successful");
    fixture.behavior.reject = false;

    results.clear();
    fixture.behavior.delay = 2200;
    const QRect late(40, 50, 440, 330);
    service.placeWindow(fixture.id(), late);
    check(until([&] { return !results.isEmpty(); }) && !results.last().success
          && results.last().message.contains(QStringLiteral("未能确认")), "timeout asserted completion");
    check(until([&] { return matches(fixture.window, late); }), "late request fixture never executed");

    results.clear();
    fixture.behavior.close = true;
    fixture.behavior.delay = 100;
    service.placeWindow(fixture.id(), QRect(50, 60, 450, 340));
    check(until([&] { return !results.isEmpty(); }) && !results.last().success
          && !IsWindow(fixture.window), "closed target reported success");

    {
        Fixture pending;
        if (!pending.window) return 2;
        auto doomed = std::make_unique<crossdesk::ui::LiveService>();
        pending.behavior.delay = 700;
        doomed->placeWindow(pending.id(), target);
        check(until([&] { return pending.behavior.entered.load(); }), "destruction fixture not reached");
        elapsed.restart();
        doomed.reset();
        check(elapsed.elapsed() < 100, "service destruction waited for a foreign window");
        check(until([&] { return matches(pending.window, target); }), "detached worker did not finish safely");
    }
    results.clear();
    service.activateWindow(0);
    check(until([&] { return !results.isEmpty(); }) && !results.last().success,
          "invalid activation was reported successful");
    return failures ? 1 : 0;
}
