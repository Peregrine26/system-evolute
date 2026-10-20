#include "ui/fake_service.h"
#include "ui/main_window.h"

#ifdef CROSSDESK_HAS_LIVE_SCANNER
#include "ui/live_service.h"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QListWidget>
#include <QListWidgetItem>
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

    // 默认铺满整个显示器：这不是一个"应用窗口"，而是一层盖在桌面上的总览。
    // --windowed 留给调试和截图对照用。
    if (args.contains(QStringLiteral("--windowed"))) {
        window.resize(1440, 900);
        window.show();
    } else {
        window.showFullScreen();
    }

    const QString shotPath = argValue(args, QStringLiteral("--shot"));
    if (!shotPath.isEmpty()) {
        // --shot-select：勾两个窗口，让画面自动切到排列结果，用来截"排完之后"的图。
        const bool selectShot = args.contains(QStringLiteral("--shot-select")) ||
                                args.contains(QStringLiteral("--shot-preview"));
        QTimer::singleShot(360, &window, [&window, shotPath, selectShot] {
            if (selectShot) {
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
            }
            // 勾满两个会触发一次淡出淡入，等它跑完再截。
            const int delay = selectShot ? 520 : 0;
            QTimer::singleShot(delay, &window, [&window, shotPath] {
                window.grab().save(shotPath);
                QCoreApplication::quit();
            });
        });
    }

    return app.exec();
}
