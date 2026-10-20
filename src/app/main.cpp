#include "ui/fake_workspace_service.h"
#include "ui/overview_window.h"

#include <QApplication>
#include <QPixmap>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    // 现在接的是假数据。等 B 的 WorkspaceManager 就绪后，
    // 这里换成真实实现，OverviewWindow 一行都不用改。
    crossdesk::ui::FakeWorkspaceService service;
    crossdesk::ui::OverviewWindow window(&service);
    window.show();

    // --shot <路径>：截一张界面图然后退出，供讲解卡和答辩材料使用。
    // 用 100ms 而不是 0：0 时首帧还没画完，截出来是空白窗口。
    const QStringList args = application.arguments();
    const int shotIndex = args.indexOf(QStringLiteral("--shot"));
    if (shotIndex > 0 && shotIndex + 1 < args.size()) {
        const QString path = args.at(shotIndex + 1);
        QTimer::singleShot(100, &application, [&window, path, &application] {
            const bool ok = window.grab().save(path);
            application.exit(ok ? 0 : 1);
        });
    }

    return application.exec();
}
