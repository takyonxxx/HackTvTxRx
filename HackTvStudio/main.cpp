#include "ui/StudioWindow.h"
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QtWidgets/QStyleFactory>
#include <QDebug>

int main(int argc, char *argv[])
{
    // Qt Multimedia: use the bundled FFmpeg backend (video file sources for TV TX)
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    qputenv("AV_LOG_FORCE_NOCOLOR", "1");

    QApplication app(argc, argv);

    QApplication::setApplicationName("HackTvStudio");
    QApplication::setOrganizationName("MarenRobotics");
    QApplication::setApplicationVersion("2.0");
    app.setWindowIcon(QIcon(":/ico/hacktv.ico"));

    qDebug() << "Starting HackTvStudio...";
    qDebug() << "Qt Version:" << qVersion();

    app.setStyle(QStyleFactory::create("Fusion"));

#ifdef Q_OS_ANDROID
    // Head-unit screens are short; shrink the global font so the dense SDR
    // panels fit and leave more room for the spectrum / waterfall.
    {
        QFont f = app.font();
        qreal ps = f.pointSizeF() > 0 ? f.pointSizeF() : 10.0;
        f.setPointSizeF(ps * 0.85);
        app.setFont(f);
    }
#endif

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
    app.setPalette(darkPalette);

    // Cyber tech theme for the HackRF SDR controls
    app.setStyleSheet(
        "QToolTip { color: #c8f0ff; background-color: #141828; border: 1px solid #00d4ff; padding: 3px; border-radius: 3px; }"

        "QGroupBox { border: 1px solid #0096c8; border-radius: 6px; margin-top: 1em; padding-top: 0.8em; background-color: rgba(0, 150, 200, 5); }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #00ffcc; font-weight: bold; text-transform: uppercase; font-size: 11px; }"

        "QPushButton { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #005a7a, stop: 1 #003d55); "
        "    color: #c8f0ff; border: 1px solid #0096c8; border-radius: 4px; padding: 6px 15px; "
        "    font-weight: bold; text-transform: uppercase; font-size: 11px; }"
        "QPushButton:hover { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #007aa0, stop: 1 #005575); border: 1px solid #00d4ff; }"
        "QPushButton:pressed { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #003d55, stop: 1 #002a3a); }"
        "QPushButton:disabled { background: #1a2030; color: #506070; border-color: #2a3545; }"

        "QLineEdit { border: 1px solid #0078a0; border-radius: 4px; padding: 5px; background-color: rgba(8, 10, 14, 200); color: #c8f0ff; selection-background-color: #0096c8; }"
        "QLineEdit:focus { border: 2px solid #00d4ff; background-color: rgba(0, 150, 200, 10); }"
        "QLineEdit:disabled { background-color: #151820; color: #506070; border-color: #2a3545; }"

        "QComboBox { border: 1px solid #0078a0; border-radius: 4px; padding: 4px 8px; background-color: rgba(8, 10, 14, 200); color: #c8f0ff; min-width: 6em; }"
        "QComboBox:hover { border: 2px solid #00d4ff; }"
        "QComboBox:disabled { background-color: #151820; color: #506070; border-color: #2a3545; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 20px; border-left: 1px solid #0078a0; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #003d55, stop: 1 #002a3a); }"
        "QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 6px solid #00d4ff; width: 0; height: 0; }"
        "QComboBox QAbstractItemView { border: 1px solid #0096c8; selection-background-color: #0078a0; background-color: #0a0c12; padding: 2px; }"

        "QSlider::groove:horizontal { border: 1px solid #0078a0; height: 6px; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #0a0c12, stop: 0.5 #003d55, stop: 1 #0a0c12); margin: 2px 0; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #00ffcc, stop: 1 #0096c8); "
        "    border: 1px solid #00d4ff; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #00ffdd, stop: 1 #00b8e6); }"
        "QSlider::groove:vertical { border: 1px solid #0078a0; width: 6px; background: #0a0c12; border-radius: 3px; }"
        "QSlider::handle:vertical { background: #FFC300; height: 14px; margin: 0 -4px; border-radius: 7px; }"

        "QTabWidget::pane { border: 1px solid #0078a0; background-color: rgba(10, 12, 18, 200); border-radius: 4px; }"
        "QTabBar::tab { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #1a2535, stop: 1 #0a0c12); color: #708090; "
        "    padding: 8px 22px; border: 1px solid #0078a0; border-bottom: none; margin-right: 2px; font-weight: bold; letter-spacing: 1px; }"
        "QTabBar::tab:selected { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #005a7a, stop: 1 #003d55); color: #00ffcc; border-color: #00d4ff; }"
        "QTabBar::tab:hover:!selected { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #2a3545, stop: 1 #1a2030); color: #a0c0d0; }"

        "QCheckBox { spacing: 8px; color: #c8f0ff; font-size: 13px; font-weight: 500; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; }"
        "QCheckBox::indicator:unchecked { border: 2px solid #00b8e6; background: #141c28; }"
        "QCheckBox::indicator:unchecked:hover { border: 2px solid #00d4ff; background: #1a3040; }"
        "QCheckBox::indicator:checked { border: 2px solid #00ffcc; background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 1, stop: 0 #00d4ff, stop: 0.5 #0096c8, stop: 1 #007aa0); }"
        "QCheckBox::indicator:disabled { border: 2px solid #2a3545; background-color: #151820; }"

        // Radio buttons: a clear ring, filled with a bright dot when selected
        "QRadioButton { spacing: 8px; color: #c8f0ff; font-size: 13px; font-weight: 500; }"
        "QRadioButton::indicator { width: 18px; height: 18px; border-radius: 10px; }"
        "QRadioButton::indicator:unchecked { border: 2px solid #00b8e6; background: #141c28; }"
        "QRadioButton::indicator:unchecked:hover { border: 2px solid #00d4ff; background: #1a3040; }"
        "QRadioButton::indicator:checked { border: 2px solid #00ffcc; "
        "    background: qradialgradient(cx: 0.5, cy: 0.5, radius: 0.5, fx: 0.5, fy: 0.5, stop: 0 #00ffcc, stop: 0.45 #00ffcc, stop: 0.55 #141c28, stop: 1 #141c28); }"
        "QRadioButton::indicator:disabled { border: 2px solid #2a3545; background-color: #151820; }"
        "QRadioButton:disabled, QCheckBox:disabled { color: #506070; }"

        "QSpinBox, QDoubleSpinBox { border: 1px solid #0078a0; border-radius: 4px; padding: 3px 20px 3px 4px; background-color: rgba(8, 10, 14, 200); color: #c8f0ff; selection-background-color: #0096c8; }"
        "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 18px; height: 11px; border-left: 1px solid #0078a0; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #003d55, stop: 1 #002a3a); }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 18px; height: 11px; border-left: 1px solid #0078a0; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #003d55, stop: 1 #002a3a); }"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-bottom: 5px solid #00d4ff; width: 0; height: 0; }"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #00d4ff; width: 0; height: 0; }"

        "QSplitter::handle:vertical { height: 6px; background: #0f1a24; border-top: 1px solid #0078a0; border-bottom: 1px solid #0078a0; }"
        "QStatusBar { color: #c8f0ff; }"

        "QProgressBar { border: 1px solid #0078a0; border-radius: 4px; background: #0a0c12; text-align: center; color: #00ffcc; }"
        "QProgressBar::chunk { background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #1f7a3f, stop: 0.7 #00cc66, stop: 1 #ffd700); border-radius: 3px; }"

        "QScrollBar:vertical { border: none; background: #0a0c12; width: 12px; border-radius: 6px; }"
        "QScrollBar::handle:vertical { background: #0078a0; min-height: 20px; border-radius: 6px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar:horizontal { border: none; background: #0a0c12; height: 12px; border-radius: 6px; }"
        "QScrollBar::handle:horizontal { background: #0078a0; min-width: 20px; border-radius: 6px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
    );

#ifdef Q_OS_ANDROID
    // Readable sizes on the head-unit screen: tab labels, group titles and the
    // value boxes shrank with the scaled app font; pin them to explicit sizes.
    app.setStyleSheet(app.styleSheet() +
        "QTabBar::tab { font-size: 14px; font-weight: bold; padding: 7px 18px; min-width: 90px; }"
        "QGroupBox { font-size: 13px; }"
        "QGroupBox::title { font-size: 13px; font-weight: bold; }"
        "QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit { font-size: 13px; min-height: 26px; }"
        "QComboBox QAbstractItemView { font-size: 14px; }"
        "QPushButton { font-size: 12px; }"
        "QStatusBar, QStatusBar QLabel { font-size: 12px; }");
#endif
    StudioWindow window;
    window.show();

    return app.exec();
}
