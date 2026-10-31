#include "Theme.h"

#include <QApplication>

namespace {

Theme g_current;
bool  g_haveCurrent = false;

QString rgba(const QColor& c, int a)
{
    return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
}

} // namespace

const QVector<Theme>& Themes::all()
{
    static const QVector<Theme> themes = {
        // id, name,
        // window, base, alt, button, text, dimText, accent, accent2, border, fieldBg,
        // plotBg, plotGrid, plotText, wfBg, fftLine, fillTop, fillBottom
        // First entry = default. SDR# look: neutral dark greys, light-blue accents, white -> blue spectrum fill
        { "gray", QString::fromUtf8("SDR# gri"),
          QColor(64, 64, 68), QColor(46, 46, 50), QColor(82, 82, 88), QColor(96, 96, 104),
          QColor(238, 238, 242), QColor(184, 184, 194), QColor(130, 205, 255), QColor(95, 175, 245), QColor(128, 128, 138), QColor(40, 40, 44),
          QColor(32, 32, 36), QColor(122, 122, 132, 120), QColor(224, 224, 232), QColor(22, 22, 26),
          QColor(250, 250, 255), QColor(208, 224, 246), QColor(16, 40, 140) },

        { "slate", QString::fromUtf8("Gök mavisi"),
          QColor(60, 76, 108), QColor(48, 62, 92), QColor(74, 92, 128), QColor(88, 108, 148),
          QColor(242, 247, 255), QColor(196, 210, 230), QColor(80, 255, 215), QColor(90, 215, 255), QColor(110, 180, 240), QColor(42, 54, 82),
          QColor(40, 56, 90), QColor(130, 175, 225, 120), QColor(228, 240, 255), QColor(28, 42, 76),
          QColor(250, 253, 255), QColor(120, 220, 255), QColor(40, 74, 136) },

        { "day", QString::fromUtf8("Gündüz (açık)"),
          QColor(228, 233, 241), QColor(255, 255, 255), QColor(240, 244, 250), QColor(206, 216, 232),
          QColor(24, 34, 50), QColor(96, 110, 130), QColor(0, 120, 110), QColor(0, 130, 210), QColor(130, 160, 200), QColor(255, 255, 255),
          QColor(246, 249, 253), QColor(172, 190, 212, 170), QColor(44, 60, 82), QColor(230, 237, 247),
          QColor(10, 70, 150), QColor(70, 150, 235), QColor(205, 226, 248) },

        { "amber", QString::fromUtf8("Sıcak (turuncu)"),
          QColor(78, 68, 60), QColor(64, 56, 50), QColor(96, 84, 74), QColor(118, 102, 88),
          QColor(255, 246, 232), QColor(214, 200, 182), QColor(255, 190, 60), QColor(255, 170, 70), QColor(232, 165, 80), QColor(56, 48, 42),
          QColor(54, 46, 42), QColor(180, 148, 108, 130), QColor(255, 236, 204), QColor(44, 36, 32),
          QColor(255, 236, 190), QColor(255, 196, 90), QColor(96, 66, 36) },

        // Same greys with orange buttons / accents
        { "grayorange", QString::fromUtf8("Gri + turuncu"),
          QColor(64, 64, 68), QColor(46, 46, 50), QColor(82, 82, 88), QColor(222, 122, 28),
          QColor(240, 240, 244), QColor(190, 190, 198), QColor(255, 170, 60), QColor(255, 150, 50), QColor(210, 125, 45), QColor(40, 40, 44),
          QColor(32, 32, 36), QColor(122, 122, 132, 120), QColor(224, 224, 232), QColor(22, 22, 26),
          QColor(250, 250, 255), QColor(208, 224, 246), QColor(16, 40, 140) },

        // The original desktop look: deep-space navy, cyan accents (default on desktop)
        { "cyber", QString::fromUtf8("Cyber (orijinal koyu)"),
          QColor(10, 12, 18), QColor(8, 10, 14), QColor(20, 28, 40), QColor(0, 61, 85),
          QColor(200, 240, 255), QColor(112, 128, 144), QColor(0, 255, 204), QColor(0, 212, 255), QColor(0, 120, 160), QColor(8, 10, 14),
          QColor(4, 14, 22), QColor(28, 105, 168, 90), QColor(180, 220, 180), QColor(2, 8, 30),
          QColor(240, 248, 255), QColor(120, 180, 255), QColor(1, 6, 35) },

        { "green", QString::fromUtf8("Yeşil"),
          QColor(50, 76, 68), QColor(40, 62, 56), QColor(64, 94, 84), QColor(80, 114, 102),
          QColor(236, 252, 242), QColor(186, 214, 200), QColor(90, 255, 180), QColor(110, 240, 200), QColor(80, 200, 150), QColor(34, 52, 46),
          QColor(36, 58, 52), QColor(110, 180, 150, 130), QColor(222, 246, 232), QColor(26, 44, 40),
          QColor(226, 255, 240), QColor(96, 236, 176), QColor(32, 86, 66) },
    };
    return themes;
}

const Theme& Themes::byId(const QString& id)
{
    for (const Theme& t : all())
        if (t.id == id) return t;
    return all().first();
}

const Theme& Themes::current()
{
    return g_haveCurrent ? g_current : all().first();
}

QPalette Themes::palette(const Theme& t)
{
    QPalette p;
    p.setColor(QPalette::Window, t.window);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.base);
    p.setColor(QPalette::AlternateBase, t.alt);
    p.setColor(QPalette::ToolTipBase, t.alt);
    p.setColor(QPalette::ToolTipText, t.text);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.button);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::BrightText, t.accent);
    p.setColor(QPalette::Link, t.accent2);
    p.setColor(QPalette::Highlight, t.border);
    p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    p.setColor(QPalette::Disabled, QPalette::Text, t.dimText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.dimText);
    p.setColor(QPalette::Disabled, QPalette::WindowText, t.dimText);
    return p;
}

QString Themes::styleSheet(const Theme& t)
{
    const bool light = t.window.lightness() > 128;
    const QColor btnTop  = light ? t.button.lighter(108) : t.button.lighter(122);
    const QColor btnBot  = light ? t.button.darker(108)  : t.button.darker(112);
    const QColor hovTop  = light ? t.button.lighter(114) : t.button.lighter(140);
    const QColor hovBot  = light ? t.button               : t.button.lighter(108);
    const QColor prsTop  = light ? t.button.darker(112)  : t.button.darker(115);
    const QColor prsBot  = light ? t.button.darker(120)  : t.button.darker(130);
    const QColor disBg   = light ? t.window.darker(104)  : t.window.darker(110);
    const QColor disBd   = light ? t.border.lighter(120) : t.border.darker(150);
    const QColor altHov  = light ? t.alt.darker(106)     : t.alt.lighter(120);
    const QColor tabTop  = light ? t.alt                  : t.alt.lighter(118);
    const QColor tabBot  = light ? t.button               : t.fieldBg;

    QString s =
        "QToolTip { color: %TEXT%; background-color: %ALT%; border: 1px solid %ACC2%; padding: 3px; border-radius: 3px; }"

        "QGroupBox { border: 1px solid %BORDER%; border-radius: 6px; margin-top: 1em; padding-top: 0.8em; background-color: %GRPBG%; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: %ACC%; font-weight: bold; text-transform: uppercase; font-size: 11px; }"

        "QPushButton { "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 %BTNTOP%, stop: 1 %BTNBOT%); "
        "    color: %TEXT%; border: 1px solid %BORDER%; border-radius: 4px; padding: 6px 15px; "
        "    font-weight: bold; text-transform: uppercase; font-size: 11px; }"
        "QPushButton:hover { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 %HOVTOP%, stop: 1 %HOVBOT%); border: 1px solid %ACC2%; }"
        "QPushButton:pressed { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 %PRSTOP%, stop: 1 %PRSBOT%); }"
        "QPushButton:disabled { background: %DISBG%; color: %DIM%; border-color: %DISBD%; }"

        "QLineEdit { border: 1px solid %BORDER%; border-radius: 4px; padding: 5px; background-color: %FIELD%; color: %TEXT%; selection-background-color: %BORDER%; }"
        "QLineEdit:focus { border: 2px solid %ACC2%; }"
        "QLineEdit:disabled { background-color: %DISBG%; color: %DIM%; border-color: %DISBD%; }"

        "QComboBox { border: 1px solid %BORDER%; border-radius: 4px; padding: 4px 8px; background-color: %FIELD%; color: %TEXT%; min-width: 6em; }"
        "QComboBox:hover { border: 2px solid %ACC2%; }"
        "QComboBox:disabled { background-color: %DISBG%; color: %DIM%; border-color: %DISBD%; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 20px; border-left: 1px solid %BORDER%; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 %BTNTOP%, stop: 1 %BTNBOT%); }"
        "QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 6px solid %ACC2%; width: 0; height: 0; }"
        "QComboBox QAbstractItemView { border: 1px solid %BORDER%; selection-background-color: %BORDER%; selection-color: %TEXT%; background-color: %BASE%; color: %TEXT%; padding: 2px; }"

        "QSlider::groove:horizontal { border: 1px solid %BORDER%; height: 6px; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 %FIELD%, stop: 0.5 %BTNBOT%, stop: 1 %FIELD%); margin: 2px 0; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 %ACC%, stop: 1 %ACC2%); "
        "    border: 1px solid %ACC2%; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: %ACC%; }"
        "QSlider::groove:vertical { border: 1px solid %BORDER%; width: 6px; background: %FIELD%; border-radius: 3px; }"
        "QSlider::handle:vertical { background: #FFC300; height: 14px; margin: 0 -4px; border-radius: 7px; }"

        "QTabWidget::pane { border: 1px solid %BORDER%; background-color: %PANE%; border-radius: 4px; }"
        "QTabBar::tab { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 %TABTOP%, stop: 1 %TABBOT%); color: %DIM%; "
        "    padding: 8px 22px; border: 1px solid %BORDER%; border-bottom: none; margin-right: 2px; font-weight: bold; letter-spacing: 1px; }"
        "QTabBar::tab:selected { background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 %HOVTOP%, stop: 1 %BTNBOT%); color: %TEXT%; border-color: %ACC2%; }"
        "QTabBar::tab:hover:!selected { background: %ALTHOV%; color: %TEXT%; }"

        "QCheckBox { spacing: 8px; color: %TEXT%; font-size: 13px; font-weight: 500; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; }"
        "QCheckBox::indicator:unchecked { border: 2px solid %BORDER%; background: %ALT%; }"
        "QCheckBox::indicator:unchecked:hover { border: 2px solid %ACC2%; background: %ALTHOV%; }"
        "QCheckBox::indicator:checked { border: 2px solid %ACC%; background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 1, stop: 0 %ACC2%, stop: 1 %BORDER%); }"
        "QCheckBox::indicator:disabled { border: 2px solid %DISBD%; background-color: %DISBG%; }"

        "QRadioButton { spacing: 8px; color: %TEXT%; font-size: 13px; font-weight: 500; }"
        "QRadioButton::indicator { width: 18px; height: 18px; border-radius: 10px; }"
        "QRadioButton::indicator:unchecked { border: 2px solid %BORDER%; background: %ALT%; }"
        "QRadioButton::indicator:unchecked:hover { border: 2px solid %ACC2%; background: %ALTHOV%; }"
        "QRadioButton::indicator:checked { border: 2px solid %ACC%; "
        "    background: qradialgradient(cx: 0.5, cy: 0.5, radius: 0.5, fx: 0.5, fy: 0.5, stop: 0 %ACC%, stop: 0.45 %ACC%, stop: 0.55 %ALT%, stop: 1 %ALT%); }"
        "QRadioButton::indicator:disabled { border: 2px solid %DISBD%; background-color: %DISBG%; }"
        "QRadioButton:disabled, QCheckBox:disabled { color: %DIM%; }"

        "QSpinBox, QDoubleSpinBox { border: 1px solid %BORDER%; border-radius: 4px; padding: 3px 20px 3px 4px; background-color: %FIELD%; color: %TEXT%; selection-background-color: %BORDER%; }"
        "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 18px; height: 11px; border-left: 1px solid %BORDER%; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 %BTNTOP%, stop: 1 %BTNBOT%); }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 18px; height: 11px; border-left: 1px solid %BORDER%; "
        "    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 %BTNTOP%, stop: 1 %BTNBOT%); }"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-bottom: 5px solid %ACC2%; width: 0; height: 0; }"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid %ACC2%; width: 0; height: 0; }"

        "QSplitter::handle:vertical { height: 6px; background: %ALT%; border-top: 1px solid %BORDER%; border-bottom: 1px solid %BORDER%; }"
        "QSplitter::handle:horizontal { width: 6px; background: %ALT%; border-left: 1px solid %BORDER%; border-right: 1px solid %BORDER%; }"
        "QStatusBar { color: %TEXT%; }"
        "QDialog { background-color: %WIN%; }"

        "QProgressBar { border: 1px solid %BORDER%; border-radius: 4px; background: %FIELD%; text-align: center; color: %ACC%; }"
        "QProgressBar::chunk { background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #1f7a3f, stop: 0.7 #00cc66, stop: 1 #ffd700); border-radius: 3px; }"

        "QScrollBar:vertical { border: none; background: %FIELD%; width: 12px; border-radius: 6px; }"
        "QScrollBar::handle:vertical { background: %BORDER%; min-height: 20px; border-radius: 6px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar:horizontal { border: none; background: %FIELD%; height: 12px; border-radius: 6px; }"
        "QScrollBar::handle:horizontal { background: %BORDER%; min-width: 20px; border-radius: 6px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }";

#ifdef Q_OS_ANDROID
    // Readable sizes on the head-unit screen: tab labels, group titles and the
    // value boxes shrank with the scaled app font; pin them to explicit sizes.
    s +=
        "QTabBar::tab { font-size: 14px; font-weight: bold; padding: 7px 18px; min-width: 90px; }"
        "QGroupBox { font-size: 13px; }"
        "QGroupBox::title { font-size: 13px; font-weight: bold; }"
        "QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit { font-size: 13px; min-height: 26px; }"
        "QComboBox QAbstractItemView { font-size: 14px; }"
        "QPushButton { font-size: 12px; }"
        "QLabel { font-size: 13px; }"
        "QStatusBar, QStatusBar QLabel { font-size: 12px; }";
#endif

    s.replace("%TEXT%",   t.text.name());
    s.replace("%DIM%",    t.dimText.name());
    s.replace("%ACC2%",   t.accent2.name());
    s.replace("%ACC%",    t.accent.name());
    s.replace("%BORDER%", t.border.name());
    s.replace("%FIELD%",  t.fieldBg.name());
    s.replace("%BASE%",   t.base.name());
    s.replace("%WIN%",    t.window.name());
    s.replace("%ALTHOV%", altHov.name());
    s.replace("%ALT%",    t.alt.name());
    s.replace("%BTNTOP%", btnTop.name());
    s.replace("%BTNBOT%", btnBot.name());
    s.replace("%HOVTOP%", hovTop.name());
    s.replace("%HOVBOT%", hovBot.name());
    s.replace("%PRSTOP%", prsTop.name());
    s.replace("%PRSBOT%", prsBot.name());
    s.replace("%DISBG%",  disBg.name());
    s.replace("%DISBD%",  disBd.name());
    s.replace("%TABTOP%", tabTop.name());
    s.replace("%TABBOT%", tabBot.name());
    s.replace("%PANE%",   rgba(t.base, 200));
    s.replace("%GRPBG%",  rgba(t.accent2, light ? 14 : 22));
    return s;
}

void Themes::apply(const Theme& t)
{
    g_current = t;
    g_haveCurrent = true;
    if (qApp) {
        qApp->setPalette(palette(t));
        qApp->setStyleSheet(styleSheet(t));
    }
}

QString Themes::accentLabelCss()
{
    return QString("QLabel { color: %1; font-weight: bold; }").arg(current().accent.name());
}

QString Themes::dimLabelCss()
{
    return QString("QLabel { color: %1; font-weight: bold; }").arg(current().dimText.name());
}
