#include "ui/fake_service.h"
#include "ui/main_window.h"

#ifdef CROSSDESK_HAS_LIVE_SCANNER
#include "ui/live_service.h"
#endif

#include <QApplication>
#include <QCoreApplication>
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
    // --fake 保留一个不依赖平台接口的完整可演示版本；
    // --fake-many 给八个窗口，用来看"卡片叠起来"的样子。
    std::unique_ptr<crossdesk::ui::CrossDeskService> service;
    const bool forceFake = args.contains(QStringLiteral("--fake")) ||
                           args.contains(QStringLiteral("--fake-many"));
#ifdef CROSSDESK_HAS_LIVE_SCANNER
    if (!forceFake) {
        service = std::make_unique<crossdesk::ui::LiveService>();
    }
#endif
    if (!service) {
        const int count = args.contains(QStringLiteral("--fake-many")) ? 8 : 4;
        service = std::make_unique<crossdesk::ui::FakeService>(nullptr, count);
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
        QTimer::singleShot(420, &window, [&window, shotPath] {
            window.grab().save(shotPath);
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
