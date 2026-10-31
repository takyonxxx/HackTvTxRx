#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QPalette>
#include <QString>
#include <QVector>

/*
 * Colour themes. One Theme drives the widget palette + stylesheet and the
 * spectrum / waterfall colours, so the whole app switches at once (the
 * "cyber dark" look was too gloomy on a car head unit).
 */
struct Theme
{
    QString id;
    QString name;
    // widgets
    QColor window, base, alt, button, text, dimText, accent, accent2, border, fieldBg;
    // spectrum / waterfall
    QColor plotBg, plotGrid, plotText, wfBg, fftLine, fillTop, fillBottom;
};

namespace Themes
{
    const QVector<Theme>& all();
    const Theme& byId(const QString& id);          // unknown id -> first (default) theme
    const Theme& current();
    void apply(const Theme& t);                    // palette + stylesheet on qApp, remembers current()
    QPalette palette(const Theme& t);
    QString styleSheet(const Theme& t);
    QString accentLabelCss();                      // "QLabel { color: <accent>; font-weight: bold; }"
    QString dimLabelCss();                         // "QLabel { color: <dim>;    font-weight: bold; }"
}

#endif // THEME_H
