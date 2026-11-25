#include <QApplication>
#include "desktop/store.h"
#include "desktop/workbench.h"
#include "desktop/language.h"
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#ifdef CROSSDESK_WITH_WINDOW_API
#include "desktop/window_overview.h"
#endif
#ifdef Q_OS_WIN
#include "desktop/global_shortcuts.h"
#endif

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    application.setOrganizationName(QStringLiteral("CrossDesk"));
    application.setApplicationName(QStringLiteral("CrossDesk"));
    application.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"data-dir", "Workspace directory", "directory"});
    parser.addOption({"background", "Start in system tray"});
    parser.addOption({"desktop-tools", "Open desktop tools instead of the window overview"});
    parser.addOption({"smoke", "Run isolated desktop widget smoke test and exit"});
    parser.addOption({"quit-after", "Exit after milliseconds (for isolated validation)", "milliseconds"});
    parser.process(application);
    QTemporaryDir sandbox;
    const bool smoke = parser.isSet("smoke");
    if (smoke) qputenv("CROSSDESK_OFFLINE", "1");
    const auto directory = smoke ? sandbox.path() : parser.isSet("data-dir") ? parser.value("data-dir") :
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/CrossDesk";
    if (!QDir().mkpath(directory)) return 1;
    auto canonical = QFileInfo(directory).canonicalFilePath();
#ifdef Q_OS_WIN
    canonical = canonical.toLower();
#endif
    const auto instanceName = "CrossDesk-" + QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    QLockFile lock(directory + "/session.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        QLocalSocket existing; existing.connectToServer(instanceName);
        if (existing.waitForConnected(1000)) {
            existing.write(parser.isSet("background") ? "background\n" : parser.isSet("desktop-tools") ? "tools\n" : "show\n");
            existing.waitForBytesWritten(1000); return 0;
        }
        if (!smoke) QMessageBox::information(nullptr, QStringLiteral("CrossDesk"), QStringLiteral("该工作区已有实例，正在启动或暂时无响应，请稍后重试。"));
        return 1;
    }
    QLocalServer server;
#ifndef Q_OS_WIN
    // The lock establishes sole ownership before a stale Unix socket is removed.
    QLocalServer::removeServer(instanceName);
#endif
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(instanceName)) return 1;
    crossdesk::desktop::Store store(directory);
    QString error;
    if (!store.load(&error)) { QMessageBox::critical(nullptr, QStringLiteral("无法读取配置"), error); return 1; }
    crossdesk::desktop::initializeLanguage(store.settings()["language"].toString("zh_CN"));
    if (smoke) {
        if (!sandbox.isValid()) return 1;
        QDir().mkpath(sandbox.path() + "/files");
        QFile fixture(sandbox.path() + "/files/example.txt");
        if (!fixture.open(QIODevice::WriteOnly)) return 1;
        fixture.write("CrossDesk native Qt desktop smoke\n"); fixture.close();
        for (const auto* kind : {"folder", "todo", "note", "clock", "weather", "music", "search"}) {
            QJsonObject data; if (QString(kind) == "folder") data["path"] = sandbox.path() + "/files";
            if (store.addWidget(kind, QString::fromUtf8(kind), data).isEmpty()) return 1;
        }
    }
    crossdesk::desktop::Workbench window(store);
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&server, &window] {
        while (auto* socket = server.nextPendingConnection()) {
            auto activate = [socket, &window] {
                if (!socket->canReadLine()) { if (socket->bytesAvailable() > 128) socket->disconnectFromServer(); return; }
                const auto command = socket->readLine(129).trimmed();
                if (command == "show") window.showPrimaryPanel();
                else if (command == "tools") window.showDesktopTools();
                socket->disconnectFromServer();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, &window, activate);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(2000, socket, [socket] { socket->disconnectFromServer(); });
            if (socket->bytesAvailable()) activate();
        }
    });
#ifdef CROSSDESK_WITH_WINDOW_API
    auto* overview = new crossdesk::desktop::WindowOverview(&window);
    window.openWindowOverview = [&window, overview] { window.hide(); overview->showOverview(); };
    window.toggleWindowOverview = [&window, overview] { if (!overview->isVisible() || overview->isMinimized()) window.hide(); overview->toggleOverview(); };
    window.hideWindowOverview = [overview] { overview->hide(); };
    overview->openDesktopTools = [&window] { window.showDesktopTools(); };
    overview->closeRequested = [&window, overview] { window.closePanel(overview); };
    window.reportShortcutStatus = [overview](const QString& message) { overview->setShortcutStatus(message); };
    const auto overviewSettings = [&store, overview] { overview->setProperty("reducedMotion", store.settings()["reducedMotion"].toBool()); };
    QObject::connect(&store, &crossdesk::desktop::Store::changed, overview, overviewSettings); overviewSettings();
#endif
#ifdef Q_OS_WIN
    crossdesk::desktop::GlobalShortcuts shortcuts(window);
    if (!smoke) {
        shortcuts.triggered = [&window](const QString& action) { window.shortcutAction(action); };
        auto previous = std::make_shared<QMap<QString, QKeySequence>>();
        auto configure = [&window, &store, &shortcuts, previous](bool force) {
            const auto settings = store.settings(); QMap<QString, QKeySequence> bindings;
            bindings["overview"] = QKeySequence(settings["shortcut_overview"].toString("Ctrl+Alt+M"));
            bindings["widgets"] = QKeySequence(settings["shortcut_widgets"].toString("F7"));
            bindings["note"] = QKeySequence(settings["shortcut_note"].toString("Ctrl+Alt+N"));
            bindings["search"] = QKeySequence(settings["shortcut_search"].toString("Ctrl+Alt+F"));
            if (!force && *previous == bindings) return; *previous = bindings;
            window.setShortcutStatus(shortcuts.configure(bindings).join(QStringLiteral(" · ")));
        };
        QObject::connect(&store, &crossdesk::desktop::Store::changed, &shortcuts, [configure] { configure(false); });
        window.retryShortcuts = [configure] { configure(true); }; configure(false);
    }
#endif
    if (!parser.isSet("background") || !QSystemTrayIcon::isSystemTrayAvailable()) {
        if (smoke || parser.isSet("desktop-tools")) window.showDesktopTools(); else window.showPrimaryPanel();
    }
    if (smoke) QTimer::singleShot(1500, &application, &QApplication::quit);
    else if (parser.isSet("quit-after")) {
        bool ok = false; const auto delay = parser.value("quit-after").toInt(&ok);
        if (ok && delay > 0) QTimer::singleShot(delay, &application, &QApplication::quit);
    }
    const int result = application.exec();
    if (smoke) std::cout << "Qt desktop smoke: " << store.widgets().size() << " widget types, exit " << result << '\n';
    return result;
}
