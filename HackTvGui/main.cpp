#include "mainwindow.h"

#include <QApplication>
#include <QtWidgets/QStyleFactory>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    a.setWindowIcon(QIcon(":/ico/hacktv.ico"));

    a.setStyle(QStyleFactory::create("Fusion"));
    // Setup palette with dark orange accents (keeping the same colors as before)
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(18, 22, 32));
    darkPalette.setColor(QPalette::WindowText, QColor(230, 235, 240));
    darkPalette.setColor(QPalette::Base, QColor(12, 15, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(22, 26, 36));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(230, 235, 240));
    darkPalette.setColor(QPalette::ToolTipText, QColor(230, 235, 240));
    darkPalette.setColor(QPalette::Text, QColor(230, 235, 240));
    darkPalette.setColor(QPalette::Button, QColor(35, 40, 55));
    darkPalette.setColor(QPalette::ButtonText, QColor(230, 235, 240));
    darkPalette.setColor(QPalette::BrightText, QColor(255, 110, 90));
    darkPalette.setColor(QPalette::Link, QColor(214, 126, 56));
    darkPalette.setColor(QPalette::Highlight, QColor(190, 100, 45));
    darkPalette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));

    // Apply the palette
    a.setPalette(darkPalette);

    // Enhanced stylesheet with dark orange accents, fixing the checkbox issue
    a.setStyleSheet(
        // Tooltips
        "QToolTip { color: #e6ebf0; background-color: #2c3e50; border: 1px solid #d97e34; padding: 2px; }"

        // Group boxes
        "QGroupBox { border: 1px solid #bc6d3d; border-radius: 6px; margin-top: 1em; padding-top: 0.8em; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #ffc186; font-weight: bold; }"

        // Buttons
        "QPushButton { background-color: #9c4a2c; color: #e6ebf0; border-radius: 4px; padding: 6px 12px; font-weight: bold; }"
        "QPushButton:hover { background-color: #bc5a3d; }"
        "QPushButton:pressed { background-color: #8c3a1c; }"
        "QPushButton:disabled { background-color: #2a3343; color: #6d7687; }"

        // Text inputs
        "QLineEdit { border: 1px solid #bc6d3d; border-radius: 4px; padding: 4px; background-color: #12151a; color: #e6ebf0; }"
        "QLineEdit:focus { border: 2px solid #d47e54; }"
        "QLineEdit:disabled { background-color: #1c2130; color: #6d7687; border-color: #2a3343; }"

        // Dropdown boxes
        "QComboBox { border: 1px solid #bc6d3d; border-radius: 4px; padding: 4px 8px; background-color: #12151a; color: #e6ebf0; min-width: 6em; }"
        "QComboBox:hover { border: 1px solid #d47e54; }"
        "QComboBox:disabled { background-color: #1c2130; color: #6d7687; border-color: #2a3343; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 20px; border-left-width: 1px; border-left-color: #bc6d3d; border-left-style: solid; border-top-right-radius: 4px; border-bottom-right-radius: 4px; }"
        "QComboBox::down-arrow { width: 14px; height: 14px; }"
        "QComboBox QAbstractItemView { border: 1px solid #bc6d3d; selection-background-color: #bc6d3d; background-color: #12151a; padding: 2px; }"

        // Sliders for frequency and gain controls
        "QSlider::groove:horizontal { border: 1px solid #bc6d3d; height: 8px; background: #12151a; margin: 2px 0; border-radius: 4px; }"
        "QSlider::handle:horizontal { background: #d47e54; border: 1px solid #ffc186; width: 16px; margin: -5px 0; border-radius: 8px; }"
        "QSlider::handle:horizontal:hover { background: #ffc186; }"

        // Progress indicators
        "QProgressBar { border: 1px solid #bc6d3d; border-radius: 4px; text-align: center; background-color: #12151a; }"
        "QProgressBar::chunk { background-color: #bc6d3d; width: 1px; }"

        // Tabs for different modes (RX/TX)
        "QTabWidget::pane { border: 1px solid #bc6d3d; border-radius: 4px; }"
        "QTabBar::tab { background-color: #281c18; color: #c0a0a0; padding: 6px 12px; border: 1px solid #bc6d3d; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
        "QTabBar::tab:selected { background-color: #9c4a2c; color: #e6ebf0; }"
        "QTabBar::tab:hover:!selected { background-color: #382924; }"

        // Updated Checkbox and Radio buttons with direct styling instead of icons
        "QCheckBox { spacing: 5px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; }"
        "QCheckBox::indicator:unchecked { border: 2px solid #bc6d3d; background-color: #12151a; border-radius: 3px; }"
        "QCheckBox::indicator:checked { border: 2px solid #bc6d3d; background-color: #9c4a2c; border-radius: 3px; }"
        "QCheckBox::indicator:hover { border: 2px solid #d47e54; }"

        "QRadioButton { spacing: 5px; }"
        "QRadioButton::indicator { width: 18px; height: 18px; }"
        "QRadioButton::indicator:unchecked { border: 2px solid #bc6d3d; background-color: #12151a; border-radius: 9px; }"
        "QRadioButton::indicator:checked { border: 2px solid #bc6d3d; background-color: #9c4a2c; border-radius: 9px; }"
        "QRadioButton::indicator:hover { border: 2px solid #d47e54; }"

        // Update the QSpinBox and QDoubleSpinBox styling in your stylesheet
        "QSpinBox, QDoubleSpinBox { border: 1px solid #bc6d3d; border-radius: 4px; padding: 4px 25px 4px 4px; background-color: #12151a; color: #e6ebf0; }"
        "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 20px; height: 12px; border-left: 1px solid #bc6d3d; border-top-right-radius: 4px; }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 20px; height: 12px; border-left: 1px solid #bc6d3d; border-bottom-right-radius: 4px; }"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { width: 6px; height: 6px; background: #e6ebf0; }"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { width: 6px; height: 6px; background: #e6ebf0; }"
        );

    MainWindow w;
    w.show();
    return a.exec();
}
