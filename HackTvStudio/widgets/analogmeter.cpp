#include "analogmeter.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>

namespace {
// Arc geometry: needle sweeps from kStartDeg (left) to kEndDeg (right), Qt angle
// convention (0 = 3 o'clock, counter-clockwise positive).
constexpr float kStartDeg = 150.0f;
constexpr float kEndDeg   = 30.0f;
// Scale is signal-to-noise in dB (channel peak above the noise floor):
// S1 = floor, 5 dB per S unit, S9 = +40 dB, then +20/+40/+60 dB over S9.
constexpr float kMinDb    = 0.0f;
constexpr float kMaxDb    = 100.0f;
constexpr float kS9Db     = 40.0f;
constexpr float kSUnitDb  = 5.0f;

float angleFor(float db)
{
    const float t = std::clamp((db - kMinDb) / (kMaxDb - kMinDb), 0.0f, 1.0f);
    return kStartDeg - t * (kStartDeg - kEndDeg);
}

QPointF onArc(const QPointF& c, float r, float deg)
{
    const float a = qDegreesToRadians(deg);
    return QPointF(c.x() + r * std::cos(a), c.y() - r * std::sin(a));
}
} // namespace

AnalogMeter::AnalogMeter(QWidget* parent)
    : QWidget(parent)
{
    // Only as tall as the gauge + readout need: no dark face stretching below it
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(230, 268);
}

void AnalogMeter::setLevel(float dbfs)
{
    m_target = std::clamp(dbfs, kMinDb, kMaxDb);
    // Needle ballistics: quick rise, slower fall (looks like a real movement)
    const float k = (m_target > m_shown) ? 0.45f : 0.25f;
    m_shown += k * (m_target - m_shown);
    update();
}

void AnalogMeter::setInfo(const QString& text)
{
    if (m_info == text) return;
    m_info = text;
    update();
}

void AnalogMeter::setClipRatio(float ratio)
{
    // Quantise so the repaint only happens when the displayed value changes
    const float q = std::round(std::max(0.0f, ratio) * 1000.0f) / 1000.0f;
    if (m_clip == q) return;
    m_clip = q;
    update();
}

void AnalogMeter::setThemeColors(const QColor& bg, const QColor& text, const QColor& accent)
{
    m_bg = bg; m_text = text; m_accent = accent;
    update();
}

void AnalogMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const int w = width(), h = height();

    // Face
    p.setPen(QPen(m_text.darker(160), 1));
    p.setBrush(m_bg);
    p.drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), 8, 8);

    // ---- Gauge (upper part): pivot below the arc, radius from the width
    const float r     = std::min(w * 0.46f, h * 0.42f);
    const float faceH = r * 1.15f + 28;                       // room used by the gauge
    const QPointF c(w / 2.0f, 18 + r + 6);                    // pivot

    // Coloured bands under the scale: S-zone (accent) up to S9 (-60 dBFS), red "+dB" above
    {
        const float rOut = r, rIn = r - 10;
        auto band = [&](float db0, float db1, const QColor& col) {
            QPainterPath path;
            const float a0 = angleFor(db0), a1 = angleFor(db1);
            path.moveTo(onArc(c, rIn, a0));
            path.arcTo(QRectF(c.x() - rOut, c.y() - rOut, 2 * rOut, 2 * rOut), a0, a1 - a0);
            path.arcTo(QRectF(c.x() - rIn, c.y() - rIn, 2 * rIn, 2 * rIn), a1, a0 - a1);
            path.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            p.drawPath(path);
        };
        QColor s = m_accent; s.setAlpha(110);
        band(kMinDb, kS9Db, s);
        band(kS9Db, kMaxDb, QColor(220, 60, 50, 150));
    }

    // Main arc
    p.setPen(QPen(m_text, 2));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), int(kEndDeg * 16), int((kStartDeg - kEndDeg) * 16));

    // Ticks + labels: S1..S9 every 5 dB from the floor, then +20/+40/+60 dB over S9
    QFont f = font();
    f.setPixelSize(std::max(10, int(r * 0.11f)));
    f.setBold(true);
    p.setFont(f);
    auto tick = [&](float db, float len, const QString& label, const QColor& col) {
        const float a = angleFor(db);
        p.setPen(QPen(col, label.isEmpty() ? 1.2 : 2));
        p.drawLine(onArc(c, r, a), onArc(c, r - len, a));
        if (!label.isEmpty()) {
            const QPointF lp = onArc(c, r - len - 11, a);
            QRectF tr(lp.x() - 18, lp.y() - 9, 36, 18);
            p.setPen(col);
            p.drawText(tr, Qt::AlignCenter, label);
        }
    };
    for (int s = 1; s <= 9; ++s) {
        const float db = (s - 1) * kSUnitDb;
        tick(db, (s % 2) ? 14 : 9, (s % 2) ? QString::number(s) : QString(), m_text);
    }
    tick(kS9Db + 20.0f, 14, "+20", QColor(255, 120, 110));
    tick(kS9Db + 40.0f, 14, "+40", QColor(255, 120, 110));
    tick(kS9Db + 60.0f, 14, "+60", QColor(255, 120, 110));
    for (float db = kSUnitDb / 2; db < kMaxDb; db += kSUnitDb) // minor ticks between the S units
        if (db < kS9Db) tick(db, 5, QString(), m_text.darker(130));
    for (float db = kS9Db + 10.0f; db < kMaxDb; db += 20.0f)  // minor ticks between +20/+40/+60
        tick(db, 5, QString(), m_text.darker(130));

    // "S" / "dB" captions
    {
        QFont cf = f; cf.setPixelSize(std::max(11, int(r * 0.14f)));
        p.setFont(cf);
        p.setPen(m_accent);
        p.drawText(QRectF(c.x() - r + 4, c.y() - r * 0.35f, 40, 20), Qt::AlignLeft | Qt::AlignVCenter, "S");
        p.setPen(QColor(255, 120, 110));
        p.drawText(QRectF(c.x() + r - 44, c.y() - r * 0.35f, 40, 20), Qt::AlignRight | Qt::AlignVCenter, "dB");
        p.setFont(f);
    }

    // Needle (with soft shadow) + pivot cap
    {
        const float a = angleFor(m_shown);
        const QPointF tipShadow = onArc(c + QPointF(1.5, 2.0), r - 2, a);
        p.setPen(QPen(QColor(0, 0, 0, 110), 3.5));
        p.drawLine(c + QPointF(1.5, 2.0), tipShadow);
        p.setPen(QPen(QColor(235, 50, 40), 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c, onArc(c, r - 2, a));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 60, 66));
        p.drawEllipse(c, 9, 9);
        p.setBrush(QColor(235, 50, 40));
        p.drawEllipse(c, 4, 4);
    }

    // ---- Readout (below the gauge)
    const float y0 = std::min(float(h) - 92.0f, faceH + 10);
    QFont big = font();
    big.setPixelSize(std::max(18, int(w * 0.14f)));
    big.setBold(true);
    p.setFont(big);
    // ADC samples at the rails are only a footnote: FM/TV at full gain clip a good
    // share of samples and still sound / look fine, so the SNR reading always stays.
    const bool clipNote = m_clip >= 0.02f;
    p.setPen(m_accent);
    p.drawText(QRectF(0, y0, w, big.pixelSize() + 8), Qt::AlignCenter, QString("+%1 dB").arg(m_shown, 0, 'f', 1));

    // Verdict for the current needle position (what the S scale means in practice)
    QString verdict;
    QColor  vcol;
    if (m_target <= 0.0f && m_shown < 0.5f) { verdict = QString::fromUtf8("—  bekliyor");          vcol = m_text.darker(140); }
    else if (m_shown < 2.5f * kSUnitDb) { verdict = QString::fromUtf8("S1–S3 · gürültü, istasyon yok"); vcol = m_text.darker(125); }
    else if (m_shown < 5.5f * kSUnitDb) { verdict = QString::fromUtf8("S4–S6 · zayıf – orta");           vcol = m_text; }
    else if (m_shown < kS9Db)    { verdict = QString::fromUtf8("S7–S9 · iyi sinyal");              vcol = m_accent; }
    else                         { verdict = QString::fromUtf8("S9+ · çok güçlü");                vcol = QColor(255, 150, 120); }
    QFont vf = font();
    vf.setPixelSize(13);
    vf.setBold(true);
    p.setFont(vf);
    p.setPen(vcol);
    p.drawText(QRectF(0, y0 + big.pixelSize() + 10, w, 18), Qt::AlignCenter, verdict);

    // Compact legend
    QFont small = font();
    small.setPixelSize(11);
    p.setFont(small);
    p.setPen(m_text.darker(140));
    const float ly = y0 + big.pixelSize() + 32;
    if (clipNote) {
        p.setPen(m_clip >= 0.20f ? QColor(255, 90, 70) : QColor(255, 190, 80));
        p.drawText(QRectF(4, ly, w - 8, 15), Qt::AlignCenter, QString::fromUtf8("ADC kırpma %%1  (gerekirse kazancı azalt)").arg(m_clip * 100.0f, 0, 'f', 1));
        p.setPen(m_text.darker(140));
    } else {
        p.drawText(QRectF(4, ly, w - 8, 15), Qt::AlignCenter, QString::fromUtf8("S1–3 gürültü  ·  S4–6 zayıf"));
    }
    p.drawText(QRectF(4, ly + 15, w - 8, 15), Qt::AlignCenter, QString::fromUtf8("S7–9 iyi  ·  S9+ çok güçlü  (SNR)"));
}
