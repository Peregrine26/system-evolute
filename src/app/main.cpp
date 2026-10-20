#include "ui/fake_service.h"
#include "ui/main_window.h"

#ifdef CROSSDESK_HAS_LIVE_SCANNER
#include "ui/live_service.h"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QStringList>
#include <QTimer>

#include <memory>

namespace {

QString argValue(const QStringList& args, const QString& key) {
    const int index = args.indexOf(key);
    if (index < 0 || index + 1 >= args.size()) {
        return QString();
    }
    return args.at(index + 1);
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const QStringList args = app.arguments();

    // 默认走真实窗口：这是 M1 要的东西，"空壳感"也主要来自假数据。
    // --fake 保留一个不依赖平台接口的完整可演示版本。
    std::unique_ptr<crossdesk::ui::CrossDeskService> service;
    const bool forceFake = args.contains(QStringLiteral("--fake"));
#ifdef CROSSDESK_HAS_LIVE_SCANNER
    if (!forceFake) {
        service = std::make_unique<crossdesk::ui::LiveService>();
    }
#endif
    if (!service) {
        service = std::make_unique<crossdesk::ui::FakeService>();
    }

    crossdesk::ui::MainWindow window(service.get());
    // 只有全局热键接入后才该真正隐藏，否则窗口会从任务栏消失且找不回来。
    window.setAllowHide(args.contains(QStringLiteral("--allow-hide")));
    window.show();

    const QString shotPath = argValue(args, QStringLiteral("--shot"));
    if (!shotPath.isEmpty()) {
        const bool previewShot = args.contains(QStringLiteral("--shot-preview"));
        // --shot-select：只勾两个窗口、留在总览页，用来截"主屏幕摆了两块"的图。
        const bool selectShot = args.contains(QStringLiteral("--shot-select"));
        QTimer::singleShot(320, &window, [&window, shotPath, previewShot, selectShot] {
            if (previewShot || selectShot) {
                // 截图辅助：从外部操作控件，不往主窗口里加测试专用 API。
                if (auto* list = window.findChild<QListWidget*>(QStringLiteral("windowList"))) {
                    int checked = 0;
                    for (int row = 0; row < list->count() && checked < 2; ++row) {
                        QListWidgetItem* item = list->item(row);
                        if (!item || !(item->flags() & Qt::ItemIsUserCheckable)) {
                            continue;
                        }
                        item->setCheckState(Qt::Checked);
                        ++checked;
                    }
                }
                if (auto* arrange =
                        window.findChild<QPushButton*>(QStringLiteral("arrangeButton"))) {
                    if (previewShot) {
                        arrange->click();
                    }
                }
            }
            const int delay = previewShot ? 420 : 0;
            QTimer::singleShot(delay, &window, [&window, shotPath] {
                window.grab().save(shotPath);
                QCoreApplication::quit();
            });
        });
    }

    return app.exec();
}
