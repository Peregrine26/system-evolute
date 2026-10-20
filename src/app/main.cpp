#include "ui/fake_workspace_service.h"
#include "ui/overview_window.h"
#include "ui/workspace_service.h"

#ifdef CROSSDESK_HAS_LIVE_SCANNER
#include "ui/live_workspace_service.h"
#endif

#include <QApplication>
#include <QPixmap>
#include <QTimer>

#include <memory>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    const QStringList args = application.arguments();

    // 默认是假数据（答辩演示要可复现）；--live 才读真实窗口。
    // 两者都只实现 WorkspaceService 抽象，OverviewWindow 一行都不用改。
    bool liveMode = false;
    std::unique_ptr<crossdesk::ui::WorkspaceService> service;
#ifdef CROSSDESK_HAS_LIVE_SCANNER
    liveMode = args.contains(QStringLiteral("--live"));
#endif
    if (liveMode) {
#ifdef CROSSDESK_HAS_LIVE_SCANNER
        service = std::make_unique<crossdesk::ui::LiveWorkspaceService>();
#endif
    } else {
        service = std::make_unique<crossdesk::ui::FakeWorkspaceService>();
    }

    crossdesk::ui::OverviewWindow window(service.get());
    if (liveMode) {
        window.setWindowTitle(QStringLiteral("CrossDesk 总览 · 实时窗口（只读）"));
    }
    window.show();

    // --shot <路径>：截一张界面图然后退出，供讲解卡和答辩材料使用。
    // 用 100ms 而不是 0：0 时首帧还没画完，截出来是空白窗口。
    const int shotIndex = args.indexOf(QStringLiteral("--shot"));
    if (shotIndex > 0 && shotIndex + 1 < args.size()) {
        const QString path = args.at(shotIndex + 1);
        QTimer::singleShot(300, &application, [&window, path, &application] {
            const bool ok = window.grab().save(path);
            application.exit(ok ? 0 : 1);
        });
    }

    return application.exec();
}
