#include "ui/fake_workspace_service.h"
#include "ui/overview_window.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    // 现在接的是假数据。等 B 的 WorkspaceManager 就绪后，
    // 这里换成真实实现，OverviewWindow 一行都不用改。
    crossdesk::ui::FakeWorkspaceService service;
    crossdesk::ui::OverviewWindow window(&service);
    window.show();

    return application.exec();
}
