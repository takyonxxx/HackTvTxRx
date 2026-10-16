#include "TestCard.h"

#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <cmath>

namespace {

constexpr int W = 768;          // 4:3 design canvas, square pixels
constexpr int H = 576;
constexpr int CX = W / 2;
constexpr int CY = H / 2;
constexpr int R  = 268;         // circle radius (outer edge of the white ring)
constexpr int RING = 6;         // white ring thickness
constexpr double SAFE = 0.80;   // circle scale: keep the ring inside the TV overscan (~10 % per edge)

/* Half chord of the circle (inner edge) at height y. */
int halfChord(int y)
{
    const double r = R - RING;
    const double dy = y - CY;
    if (std::fabs(dy) >= r) return 0;
    return static_cast<int>(std::sqrt(r * r - dy * dy));
}

void drawBackground(QPainter& p)
{
    // Mid-grey field with a white grid of squares; half squares at the edges
    p.fillRect(0, 0, W, H, QColor(120, 120, 120));
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    const int cell = 48, line = 4;
    for (int x = cell / 2 - line / 2; x < W; x += cell) p.drawRect(x, 0, line, H);
    for (int y = cell / 2 - line / 2; y < H; y += cell) p.drawRect(0, y, W, line);

    // Coloured castellations on the border, as on the PM5544
    struct Blk { int x, y, w, h; QColor c; };
    const Blk blocks[] = {
        {  26,  26,  44,  70, QColor( 70, 200, 220) },   // top left: cyan
        { 698,  26,  44,  70, QColor( 60, 110, 230) },   // top right: blue
        {   0, 148,  44, 110, QColor(220,  40,  60) },   // left: red
        {   0, 282,  44, 110, QColor(190,  40, 140) },   // left: magenta
        { 724, 148,  44, 110, QColor(190, 110,  40) },   // right: orange/brown
        { 724, 282,  44, 110, QColor( 50, 170,  90) },   // right: green
        {  26, 480,  44,  70, QColor( 40,  40,  40) },   // bottom left: dark
        { 698, 480,  44,  70, QColor( 40,  40,  40) },   // bottom right: dark
    };
    for (const Blk& b : blocks) p.fillRect(b.x, b.y, b.w, b.h, b.c);
}

/* Fill a horizontal band inside the circle with `n` equal columns of colours. */
void columns(QPainter& p, int y0, int y1, const QList<QColor>& cols)
{
    const int hc = std::min(halfChord(y0), halfChord(y1));
    const int x0 = CX - hc, w = 2 * hc;
    for (int i = 0; i < cols.size(); ++i) {
        const int a = x0 + w * i / cols.size();
        const int b = x0 + w * (i + 1) / cols.size();
        p.fillRect(a, y0, b - a, y1 - y0, cols[i]);
    }
}

void drawCircleContent(QPainter& p, const QString& label, const QString& clock)
{
    // Everything inside the ring is designed around a radius of R; shrink it
    // towards the centre so the ring stays visible on an overscanning TV.
    p.save();
    p.translate(CX, CY);
    p.scale(SAFE, SAFE);
    p.translate(-CX, -CY);
    // Clip to the inside of the ring; white background
    QPainterPath inner;
    inner.addEllipse(QPointF(CX, CY), R - RING, R - RING);
    p.save();
    p.setClipPath(inner);
    p.fillRect(0, 0, W, H, Qt::white);

    // 1. identification box (black with the label), top
    {
        const int y0 = CY - 232, y1 = CY - 186;
        p.fillRect(CX - 120, y0, 240, y1 - y0, Qt::black);
        p.setPen(Qt::white);
        QFont f("Arial", 20, QFont::Bold);
        p.setFont(f);
        p.drawText(QRect(CX - 120, y0, 240, y1 - y0), Qt::AlignCenter, label);
    }

    // 2. black / white castellation row
    {
        const int y0 = CY - 180, y1 = CY - 150;
        QList<QColor> c;
        for (int i = 0; i < 12; ++i) c << ((i & 1) ? Qt::white : Qt::black);
        columns(p, y0, y1, c);
    }

    // 3. colour bars (75 %)
    columns(p, CY - 150, CY - 95, { QColor(191, 191, 0), QColor(0, 191, 191), QColor(0, 191, 0),
                                    QColor(191, 0, 191), QColor(191, 0, 0), QColor(0, 0, 191) });

    // 4. grey scale, dark -> light
    {
        QList<QColor> g;
        for (int i = 0; i < 6; ++i) { const int v = 255 * i / 5; g << QColor(v, v, v); }
        columns(p, CY - 95, CY - 45, g);
    }

    // 5. mid-grey band carrying the horizontal line of the cross
    columns(p, CY - 45, CY + 5, { QColor(128, 128, 128) });

    // 6. frequency gratings (left half, coarse -> fine) and grey (right half)
    {
        const int y0 = CY + 5, y1 = CY + 55;
        const int hc = halfChord(y1);
        p.fillRect(CX - hc, y0, hc, y1 - y0, Qt::white);
        p.fillRect(CX, y0, hc, y1 - y0, QColor(128, 128, 128));
        // five groups of increasing line frequency
        int x = CX - hc + 8;
        for (int period = 24; period >= 6 && x < CX - 6; period -= 4) {
            const int groupW = hc * 2 / 11;
            for (int gx = x; gx < x + groupW && gx < CX - 6; gx += period)
                p.fillRect(gx, y0, period / 2, y1 - y0, Qt::black);
            x += groupW + 4;
        }
    }

    // 7. grey scale, light -> dark
    {
        QList<QColor> g;
        for (int i = 0; i < 6; ++i) { const int v = 255 * (5 - i) / 5; g << QColor(v, v, v); }
        columns(p, CY + 55, CY + 105, g);
    }

    // 8. clock box (24 h, with seconds) or, without a clock, the pulse-and-bar row
    {
        const int y0 = CY + 105, y1 = CY + 150;
        const int hc = halfChord(y1);
        if (!clock.isEmpty()) {
            p.fillRect(CX - hc, y0, 2 * hc, y1 - y0, Qt::black);
            p.setPen(Qt::white);
            QFont f("Arial", 30, QFont::Bold);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 3);
            p.setFont(f);
            p.drawText(QRect(CX - hc, y0 - 2, 2 * hc, y1 - y0), Qt::AlignCenter, clock);
        } else {
            p.fillRect(CX - hc, y0, hc, y1 - y0, Qt::black);
            p.fillRect(CX, y0, hc, y1 - y0, Qt::white);
        }
    }

    // 9. yellow field with the red centre bar at the bottom
    {
        const int y0 = CY + 150, y1 = CY + R;
        p.fillRect(CX - R, y0, 2 * R, y1 - y0, QColor(240, 220, 0));
        p.fillRect(CX - 14, y0, 28, y1 - y0, QColor(220, 30, 30));
    }

    // Cross: vertical bar through the middle rows, horizontal line at the centre
    p.fillRect(CX - 7, CY - 150, 14, 155, Qt::black);
    p.fillRect(CX - halfChord(CY), CY - 3, 2 * halfChord(CY), 6, Qt::black);

    p.restore();

    // White ring
    p.setPen(QPen(Qt::white, RING));
    p.setBrush(Qt::NoBrush);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.drawEllipse(QPointF(CX, CY), R - RING / 2.0, R - RING / 2.0);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.restore();
}

} // namespace

QImage TestCard::render(int width, int height, const QString& label, const QString& clock)
{
    QImage card(W, H, QImage::Format_RGB32);
    {
        QPainter p(&card);
        drawBackground(p);
        drawCircleContent(p, label, clock);
    }
    if (width == W && height == H) return card;
    return card.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
