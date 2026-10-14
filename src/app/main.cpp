#include <QApplication>
#include <QLabel>
#include <QMainWindow>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("CrossDesk"));
    window.resize(800, 500);

    auto* title = new QLabel(QStringLiteral("CrossDesk"));
    title->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(title);

    window.show();
    return application.exec();
}
