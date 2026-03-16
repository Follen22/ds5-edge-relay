#include "mainwindow.hpp"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    app.setApplicationName("ds5-edge-relay");
    app.setOrganizationName("ds5-edge-relay");
    app.setApplicationDisplayName("DS5 Edge Relay");

    // Не закрывать приложение когда закрывается последнее окно
    // (нужно для работы в трее)
    app.setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.show();

    return app.exec();
}
