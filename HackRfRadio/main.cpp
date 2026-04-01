#include <QApplication>
#include "radiowindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("HackRF Radio");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("MarenRobotics");

    RadioWindow w;
    w.show();

    return app.exec();
}
