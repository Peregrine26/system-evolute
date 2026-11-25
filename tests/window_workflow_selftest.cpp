#include "desktop/window_overview.h"
#include "platform/windows/win32_api_adapter.h"
#include "window_layout.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <iostream>

using namespace crossdesk;
using namespace crossdesk::desktop;
namespace {
void drain(int ms) { QElapsedTimer elapsed; elapsed.start(); do { QApplication::processEvents(); QThread::msleep(5); } while (elapsed.elapsed() < ms); }
bool same(const WindowBounds& a, const WindowBounds& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (app.arguments().contains("--fixture")) {
        QWidget left, right; left.setWindowTitle("CrossDesk workflow fixture A"); right.setWindowTitle("CrossDesk workflow fixture B");
        auto* l = new QVBoxLayout(&left); auto* r = new QVBoxLayout(&right);
        auto* first = new QLabel("Fixture A"); auto* second = new QLabel("Fixture B"); l->addWidget(first); r->addWidget(second);
        left.setGeometry(60, 70, 320, 240); right.setGeometry(410, 90, 320, 240);
        left.setAttribute(Qt::WA_ShowWithoutActivating); right.setAttribute(Qt::WA_ShowWithoutActivating); left.show(); right.show();
        QTimer timer; int frame = 0; QObject::connect(&timer, &QTimer::timeout, &app, [&] { first->setText("Fixture A frame " + QString::number(++frame)); second->setText("Fixture B frame " + QString::number(frame)); }); timer.start(100);
        QTimer::singleShot(12000, &app, &QApplication::quit); return app.exec();
    }
    QProcess fixture; auto environment = QProcessEnvironment::systemEnvironment(); environment.remove("QT_QPA_PLATFORM"); fixture.setProcessEnvironment(environment);
    fixture.start(QCoreApplication::applicationFilePath(), {"--fixture"});
    if (!fixture.waitForStarted(3000)) { std::cerr << "fixture process failed to start\n"; return 1; }
    const auto childPid = std::uint32_t(fixture.processId()); Win32ApiAdapter api;
    WindowScanResult scan; QElapsedTimer ready; ready.start();
    do { scan = WindowsWindowScanner(api).scan_with_status(); int count = 0; for (const auto& w : scan.windows) if (w.process_id == childPid && w.visible && !w.title.empty()) ++count; if (count == 2 && scan.complete) break; drain(20); } while (ready.elapsed() < 5000);
    std::array<WindowIdentity, 2> selected{}; int count = 0;
    for (const auto& w : scan.windows) if (w.process_id == childPid && w.visible && !w.title.empty() && count < 2) selected[count++] = {w.native_handle, w.process_id};
    if (count != 2) { std::cerr << "two isolated fixture windows not found\n"; fixture.kill(); fixture.waitForFinished(3000); return 1; }
    WindowOverview overview; overview.setProperty("reducedMotion", true); overview.setAttribute(Qt::WA_ShowWithoutActivating); overview.showOverview(); drain(100);
    auto* list = overview.findChild<QTreeWidget*>("windowList"); if (!list) return 1;
    list->clearSelection();
    for (int i = 0; i < list->topLevelItemCount(); ++i) if (list->topLevelItem(i)->data(0, Qt::UserRole + 1).toUInt() == childPid) list->topLevelItem(i)->setSelected(true);
    const auto rows = list->selectedItems(); if (rows.size() != 2) return 1;
    for (int i = 0; i < 2; ++i) selected[i] = {rows[i]->data(0, Qt::UserRole).toULongLong(), childPid};
    WindowBounds work; api.get_primary_work_area(work);
    const auto plan = planTwoWindows(WindowsWindowScanner(api).scan_with_status(), selected, work, TwoWindowMode::Equal, std::uint32_t(app.applicationPid()));
    if (!plan.valid()) return 1;
    overview.findChild<QPushButton*>("previewLayout")->click(); drain(120);
    bool ok = overview.property("layoutMode").toString() == "preview";
    for (int i = 0; i < 2; ++i) { WindowBounds actual; ok &= api.get_window_bounds(selected[i].handle, actual) && same(actual, plan.plan.sources[i]); }
    const auto position = app.arguments().indexOf("--screenshot");
    if (position >= 0 && position + 1 < app.arguments().size()) { overview.screen()->grabWindow(overview.winId()).save(app.arguments()[position + 1]); drain(1800); }
    overview.findChild<QPushButton*>("confirmLayout")->click(); drain(450);
    for (int i = 0; i < 2; ++i) { WindowBounds actual; ok &= api.get_window_bounds(selected[i].handle, actual) && same(actual, plan.plan.targets[i].bounds); }
    ok &= overview.property("layoutMode").toString() == "overview";
    std::cout << "Isolated workflow: preview leaves fixture bounds intact; confirm reaches " << (ok ? "both targets" : "an unexpected result") << '\n';
    overview.hide(); fixture.terminate(); if (!fixture.waitForFinished(3000)) { fixture.kill(); fixture.waitForFinished(3000); }
    return ok ? 0 : 1;
}
