#include "mainwindow.h"

#include <QApplication>
#include <QtWidgets/QStyleFactory>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>

int main(int argc, char *argv[])
{
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    qputenv("AV_LOG_FORCE_NOCOLOR", "1");
    qputenv("LIBVA_DRIVER_NAME", "");
    qputenv("VDPAU_DRIVER", "");

    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/ico/hacktv.ico"));

    a.setStyle(QStyleFactory::create("Fusion"));
    
    // Setup palette with cyber blue-green accents for SDR tech look
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(10, 12, 18));        // Deep space background
    darkPalette.setColor(QPalette::WindowText, QColor(200, 240, 255)); // Cool white-blue text
    darkPalette.setColor(QPalette::Base, QColor(8, 10, 14));          // Ultra dark base
    darkPalette.setColor(QPalette::AlternateBase, QColor(15, 18, 25)); // Slightly lighter alternate
    darkPalette.setColor(QPalette::ToolTipBase, QColor(20, 25, 35));
    darkPalette.setColor(QPalette::ToolTipText, QColor(200, 240, 255));
    darkPalette.setColor(QPalette::Text, QColor(200, 240, 255));
    darkPalette.setColor(QPalette::Button, QColor(20, 25, 40));       // Cool dark button
    darkPalette.setColor(QPalette::ButtonText, QColor(200, 240, 255));
    darkPalette.setColor(QPalette::BrightText, QColor(0, 255, 200));  // Bright cyan accent
    darkPalette.setColor(QPalette::Link, QColor(0, 200, 255));        // Electric blue links
    darkPalette.setColor(QPalette::Highlight, QColor(0, 150, 200));   // Tech blue highlight
    darkPalette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));

    // Apply the palette
    a.setPalette(darkPalette);

    // Enhanced stylesheet with cyber tech theme for HackRF SDR
    a.setStyleSheet(
        // Tooltips with tech glow
        "QToolTip { color: #c8f0ff; background-color: #141828; border: 1px solid #00d4ff; padding: 3px; border-radius: 3px; }"

        // Group boxes with neon accent
        "QGroupBox { border: 1px solid #0096c8; border-radius: 6px; margin-top: 1em; padding-top: 0.8em; background-color: rgba(0, 150, 200, 5); }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #00ffcc; font-weight: bold; text-transform: uppercase; font-size: 11px; }"

        // Buttons with cyber glow effect
        "QPushButton { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #005a7a, stop: 1 #003d55); "
        "    color: #c8f0ff; "
        "    border: 1px solid #0096c8; "
        "    border-radius: 4px; "
        "    padding: 6px 15px; "
        "    font-weight: bold; "
        "    text-transform: uppercase; "
        "    font-size: 11px; "
        "}"
        "QPushButton:hover { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #007aa0, stop: 1 #005575); "
        "    border: 1px solid #00d4ff; "
        "    box-shadow: 0 0 10px rgba(0, 212, 255, 0.3); "
        "}"
        "QPushButton:pressed { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #003d55, stop: 1 #002a3a); }"
        "QPushButton:disabled { background: #1a2030; color: #506070; border-color: #2a3545; }"

        // Text inputs with tech border
        "QLineEdit { "
        "    border: 1px solid #0078a0; "
        "    border-radius: 4px; "
        "    padding: 5px; "
        "    background-color: rgba(8, 10, 14, 200); "
        "    color: #c8f0ff; "
        "    selection-background-color: #0096c8; "
        "}"
        "QLineEdit:focus { border: 2px solid #00d4ff; background-color: rgba(0, 150, 200, 10); }"
        "QLineEdit:disabled { background-color: #151820; color: #506070; border-color: #2a3545; }"

        // Dropdown boxes with tech styling
        "QComboBox { "
        "    border: 1px solid #0078a0; "
        "    border-radius: 4px; "
        "    padding: 4px 8px; "
        "    background-color: rgba(8, 10, 14, 200); "
        "    color: #c8f0ff; "
        "    min-width: 6em; "
        "}"
        "QComboBox:hover { border: 2px solid #00d4ff; }"
        "QComboBox:disabled { background-color: #151820; color: #506070; border-color: #2a3545; }"
        "QComboBox::drop-down { "
        "    subcontrol-origin: padding; "
        "    subcontrol-position: top right; "
        "    width: 20px; "
        "    border-left: 1px solid #0078a0; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #003d55, stop: 1 #002a3a); "
        "}"
        "QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 6px solid #00d4ff; width: 0; height: 0; }"
        "QComboBox QAbstractItemView { "
        "    border: 1px solid #0096c8; "
        "    selection-background-color: #0078a0; "
        "    background-color: #0a0c12; "
        "    padding: 2px; "
        "}"

        // Sliders for frequency controls with neon look
        "QSlider::groove:horizontal { "
        "    border: 1px solid #0078a0; "
        "    height: 6px; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #0a0c12, stop: 0.5 #003d55, stop: 1 #0a0c12); "
        "    margin: 2px 0; "
        "    border-radius: 3px; "
        "}"
        "QSlider::handle:horizontal { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #00ffcc, stop: 1 #0096c8); "
        "    border: 1px solid #00d4ff; "
        "    width: 14px; "
        "    height: 14px; "
        "    margin: -5px 0; "
        "    border-radius: 7px; "
        "}"
        "QSlider::handle:horizontal:hover { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #00ffdd, stop: 1 #00b8e6); "
        "    box-shadow: 0 0 10px rgba(0, 255, 200, 0.5); "
        "}"

        // Progress bars with animated look
        "QProgressBar { "
        "    border: 1px solid #0078a0; "
        "    border-radius: 4px; "
        "    text-align: center; "
        "    background-color: #0a0c12; "
        "    color: #00ffcc; "
        "    font-weight: bold; "
        "}"
        "QProgressBar::chunk { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #003d55, stop: 0.5 #0096c8, stop: 1 #00d4ff); "
        "    border-radius: 3px; "
        "}"

        // Tabs for RX/TX modes with tech styling
        "QTabWidget::pane { "
        "    border: 1px solid #0078a0; "
        "    background-color: rgba(10, 12, 18, 200); "
        "    border-radius: 4px; "
        "}"
        "QTabBar::tab { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #1a2535, stop: 1 #0a0c12); "
        "    color: #708090; "
        "    padding: 8px 16px; "
        "    border: 1px solid #0078a0; "
        "    border-bottom: none; "
        "    margin-right: 2px; "
        "}"
        "QTabBar::tab:selected { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #005a7a, stop: 1 #003d55); "
        "    color: #00ffcc; "
        "    border-color: #00d4ff; "
        "}"
        "QTabBar::tab:hover:!selected { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #2a3545, stop: 1 #1a2030); "
        "    color: #a0c0d0; "
        "}"

        // Checkboxes and Radio buttons with cyber style
        "QCheckBox { spacing: 6px; color: #c8f0ff; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 3px; }"
        "QCheckBox::indicator:unchecked { "
        "    border: 2px solid #0078a0; "
        "    background-color: #0a0c12; "
        "}"
        "QCheckBox::indicator:checked { "
        "    border: 2px solid #00d4ff; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 1, stop: 0 #0096c8, stop: 1 #005a7a); "
        "}"
        "QCheckBox::indicator:checked::after { "
        "    content: ''; "
        "    width: 5px; "
        "    height: 10px; "
        "    border: 2px solid #00ffcc; "
        "    border-width: 0 2px 2px 0; "
        "}"
        "QCheckBox::indicator:hover { "
        "    border: 2px solid #00d4ff; "
        "    background-color: rgba(0, 150, 200, 20); "
        "}"

        "QRadioButton { spacing: 6px; color: #c8f0ff; }"
        "QRadioButton::indicator { width: 18px; height: 18px; }"
        "QRadioButton::indicator:unchecked { "
        "    border: 2px solid #0078a0; "
        "    background-color: #0a0c12; "
        "    border-radius: 9px; "
        "}"
        "QRadioButton::indicator:checked { "
        "    border: 2px solid #00d4ff; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 1, stop: 0 #00ffcc, stop: 0.5 #0096c8, stop: 1 #005a7a); "
        "    border-radius: 9px; "
        "}"
        "QRadioButton::indicator:hover { "
        "    border: 2px solid #00d4ff; "
        "    background-color: rgba(0, 150, 200, 20); "
        "}"

        // Spin boxes with tech theme
        "QSpinBox, QDoubleSpinBox { "
        "    border: 1px solid #0078a0; "
        "    border-radius: 4px; "
        "    padding: 4px 25px 4px 4px; "
        "    background-color: rgba(8, 10, 14, 200); "
        "    color: #c8f0ff; "
        "    selection-background-color: #0096c8; "
        "}"
        "QSpinBox::up-button, QDoubleSpinBox::up-button { "
        "    subcontrol-origin: border; "
        "    subcontrol-position: top right; "
        "    width: 20px; "
        "    height: 12px; "
        "    border-left: 1px solid #0078a0; "
        "    border-top-right-radius: 4px; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #003d55, stop: 1 #002a3a); "
        "}"
        "QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #005a7a, stop: 1 #003d55); }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button { "
        "    subcontrol-origin: border; "
        "    subcontrol-position: bottom right; "
        "    width: 20px; "
        "    height: 12px; "
        "    border-left: 1px solid #0078a0; "
        "    border-bottom-right-radius: 4px; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #003d55, stop: 1 #002a3a); "
        "}"
        "QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #005a7a, stop: 1 #003d55); }"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { "
        "    image: none; "
        "    border-left: 4px solid transparent; "
        "    border-right: 4px solid transparent; "
        "    border-bottom: 5px solid #00d4ff; "
        "    width: 0; "
        "    height: 0; "
        "}"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { "
        "    image: none; "
        "    border-left: 4px solid transparent; "
        "    border-right: 4px solid transparent; "
        "    border-top: 5px solid #00d4ff; "
        "    width: 0; "
        "    height: 0; "
        "}"

        // Scrollbars with cyber theme
        "QScrollBar:vertical { "
        "    border: none; "
        "    background: #0a0c12; "
        "    width: 12px; "
        "    border-radius: 6px; "
        "}"
        "QScrollBar::handle:vertical { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #003d55, stop: 0.5 #0078a0, stop: 1 #003d55); "
        "    min-height: 20px; "
        "    border-radius: 6px; "
        "}"
        "QScrollBar::handle:vertical:hover { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #005a7a, stop: 0.5 #0096c8, stop: 1 #005a7a); "
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar:horizontal { "
        "    border: none; "
        "    background: #0a0c12; "
        "    height: 12px; "
        "    border-radius: 6px; "
        "}"
        "QScrollBar::handle:horizontal { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #003d55, stop: 0.5 #0078a0, stop: 1 #003d55); "
        "    min-width: 20px; "
        "    border-radius: 6px; "
        "}"
        "QScrollBar::handle:horizontal:hover { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #005a7a, stop: 0.5 #0096c8, stop: 1 #005a7a); "
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
        );

    MainWindow w;
    w.show();
    return a.exec();
}
