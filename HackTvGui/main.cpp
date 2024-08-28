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

    // Set up a dark color scheme with yellow accents
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(218, 165, 32));  // Goldenrod
    darkPalette.setColor(QPalette::Highlight, QColor(218, 165, 32));  // Goldenrod
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    // Apply the palette to the application
    a.setPalette(darkPalette);

    // Set stylesheet for some specific widget styles with yellow accents
    a.setStyleSheet("QToolTip { color: #ffffff; background-color: #DAA520; border: 1px solid white; }"
                    "QGroupBox { border: 1px solid #DAA520; border-radius: 5px; margin-top: 0.5em; }"
                    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px 0 3px; }"
                    "QPushButton { background-color: #DAA520; color: black; border-radius: 5px; padding: 5px; }"
                    "QPushButton:hover { background-color: #FFD700; }"
                    "QLineEdit { border: 1px solid #DAA520; border-radius: 3px; padding: 2px; }"
                    "QComboBox { border: 1px solid #DAA520; border-radius: 3px; padding: 1px 18px 1px 3px; }"
                    "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 15px; border-left-width: 1px; border-left-color: #DAA520; border-left-style: solid; border-top-right-radius: 3px; border-bottom-right-radius: 3px; }"
                    "QComboBox::down-arrow { image: url(down_arrow.png); }");  // You might need to provide a custom down arrow image

    MainWindow w;
    w.show();
    return a.exec();
}
