#ifndef TESTCARD_H
#define TESTCARD_H

#include <QImage>
#include <QString>

/*
 * Philips PM5544-style test card, drawn with QPainter.
 *
 * render() paints the card on a square-pixel 4:3 canvas and then stretches it
 * to the encoder's active picture size (e.g. 832 x 576), so the circle comes
 * out round on a 4:3 television even though the encoder's pixels are not
 * square. `label` goes into the black identification box at the top. The
 * circle and its content are drawn at 80 % of the frame height so that a
 * television's overscan (up to ~10 % per edge on some sets) does not clip the ring.
 */
namespace TestCard {

/* `clock` (e.g. "23:41:07") is drawn in the black box below the centre; empty = none. */
QImage render(int width, int height, const QString& label = QStringLiteral("HackTvStudio"),
              const QString& clock = QString());

}

#endif // TESTCARD_H
