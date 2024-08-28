#include "mainwindow.h"

#include <QApplication>
#include <QtWidgets/QStyleFactory>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle(QStyleFactory::create("Fusion"));
    QPalette p (QColor(47, 47, 47));
    a.setPalette(p);
    MainWindow w;
    w.show();
    return a.exec();
}
