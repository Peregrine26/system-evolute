// A-owned integration experiment, independent of C's final overview UI.
#include "platform/windows/qt_desktop_bridge.h"

#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const bool smoke = app.arguments().contains(QStringLiteral("--smoke"));
    QWidget first, second;
    first.setAttribute(Qt::WA_QuitOnClose, false);
    second.setAttribute(Qt::WA_QuitOnClose, false);
    first.setWindowTitle(QStringLiteral("CrossDesk live source A"));
    second.setWindowTitle(QStringLiteral("CrossDesk live source B"));
    first.resize(320, 220);
    second.resize(320, 220);
    QLabel a(&first), b(&second);
    a.setGeometry(20, 20, 280, 160);
    b.setGeometry(20, 20, 280, 160);
    QTimer frames;
    int count = 0;
    QObject::connect(&frames, &QTimer::timeout, [&] {
        a.setText(QStringLiteral("A frame %1").arg(++count));
        b.setText(QStringLiteral("B frame %1").arg(count));
    });
    frames.start(100);
    first.show();
    second.show();

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("CrossDesk Qt DWM experiment"));
    window.resize(800, 500);
    auto* body = new QWidget;
    auto* layout = new QVBoxLayout(body);
    auto* status = new QLabel;
    auto* toggle = new QPushButton(QStringLiteral("Show / clear previews"));
    auto* preview_area = new QWidget;
    preview_area->setMinimumSize(300, 180);
    layout->addWidget(status);
    layout->addWidget(toggle);
    layout->addWidget(preview_area, 1);
    window.setCentralWidget(body);
    window.show();
    crossdesk::QtDesktopBridge bridge(window);
    const auto key = bridge.register_hotkey();
    const QString key_text = key.ready() ? QStringLiteral("Ctrl+Alt+M ready")
        : QStringLiteral("Shortcut unavailable (%1); button still works").arg(key.platform_error);
    bool expanded = true;
    bool previews_ready = false;
    auto update = [&] {
        if (!expanded) { bridge.clear_previews(); status->setText(key_text); return; }
        const QPoint origin = preview_area->mapTo(&window, QPoint(0, 0));
        const int half = preview_area->width() / 2;
        const auto results = bridge.set_previews({
            {static_cast<crossdesk::NativeWindowHandle>(first.winId()),
             QRect(origin, QSize(half - 6, preview_area->height()))},
            {static_cast<crossdesk::NativeWindowHandle>(second.winId()),
             QRect(origin + QPoint(half + 6, 0), QSize(preview_area->width() - half - 6, preview_area->height()))}
        });
        previews_ready = results[0].ready() && results[1].ready();
        status->setText(key_text + QStringLiteral(" | A: %1 | B: %2")
            .arg(results[0].ready() ? QStringLiteral("live") : QStringLiteral("unavailable"))
            .arg(results[1].ready() ? QStringLiteral("live") : QStringLiteral("unavailable")));
    };
    auto toggle_action = [&] { expanded = !expanded; update(); };
    bridge.toggle_overview = toggle_action;
    QObject::connect(toggle, &QPushButton::clicked, toggle_action);
    // Experiment only: refresh slots after resize. Final UI should update on
    // resize / scan / screen changes, and clear during transition animations.
    QTimer rectangles;
    QObject::connect(&rectangles, &QTimer::timeout, update);
    rectangles.start(500);
    QTimer::singleShot(0, update);
    if (smoke) QTimer::singleShot(3000, &app, &QApplication::quit);
    const int exit_code = app.exec();
    std::cout << "Qt preview registrations=" << previews_ready
              << " source frames=" << count << " hotkey=" << key.ready() << '\n';
    return smoke && (!previews_ready || count < 2) ? 1 : exit_code;
}
