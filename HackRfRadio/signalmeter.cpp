#include "signalmeter.h"
#include <QPainter>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

SignalMeter::SignalMeter(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(250, 160);
    setMaximumHeight(250);

    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(25);
    connect(m_animTimer, &QTimer::timeout, [this]() {
        bool dirty = false;
        float diff = m_level - m_displayLevel;
        if (std::abs(diff) > 0.001f) {
            m_displayLevel += diff * 0.22f;
            dirty = true;
        } else {
            m_displayLevel = m_level;
        }
        if (m_peak > m_displayLevel + 0.005f) {
            m_peak -= 0.003f;
            dirty = true;
        } else if (m_peak < m_displayLevel) {
            m_peak = m_displayLevel;
        }
        if (dirty) update();
    });
    m_animTimer->start();
}

void SignalMeter::setLevel(float level)
{
    m_level = std::clamp(level, 0.0f, 1.0f);
    if (m_level > m_peak) m_peak = m_level;
    update();
}

void SignalMeter::setTxPowerDbm(float dbm)
{
    m_txDbm = dbm;
    // Map dBm to 0.0-1.0 level for the gauge
    // HackRF TX range: -40 dBm (min) to +15 dBm (theoretical max)
    // Map: -40 = 0.0, +15 = 1.0
    float mapped = (dbm + 40.0f) / 55.0f;
    m_level = std::clamp(mapped, 0.0f, 1.0f);
    if (m_level > m_peak) m_peak = m_level;
    update();
}

void SignalMeter::setTxMode(bool tx)
{
    if (m_txMode == tx) return;
    m_txMode = tx;
    m_level = 0.0f;
    m_displayLevel = 0.0f;
    m_peak = 0.0f;
    update();
}

// Helper: convert "gauge angle" (0.0=start, 1.0=end of scale) to screen coordinates
// The gauge goes from lower-left (7 o'clock) clockwise to lower-right (5 o'clock)
// In screen coords: 0=right, CW positive (Y goes down)
// 7 o'clock = 120 degrees from top-right, measuring CW = 240 deg from right CW
// Actually let's define it simply:
//   frac=0.0 -> angle = 225 deg (screen, CW from right) = lower-left
//   frac=1.0 -> angle = -45 deg = 315 deg (screen) = lower-right
//   sweep = -270 deg CW... no that's too much
//
// Let's use a clean approach:
//   Define gauge angles in "clock" terms:
//   Start: 7:30 position = 225 deg measured CCW from 3 o'clock (standard math)
//   End: 4:30 position = -45 deg = 315 deg measured CCW from 3 o'clock
//   The needle sweeps from 225 down through 180, 90, 0, to 315
//   That's 225 -> 315 going CLOCKWISE = -270 degrees (or +90 CCW... no)
//
// Simplest: use math angles (0=right, CCW positive) and manually draw.
// Start angle (frac=0): 7 o'clock position
//   In math angles, 7 o'clock = 210 degrees (measured CCW from right)
//   But screen Y is flipped, so 7 o'clock in screen = -210 from right = 150 in screen CW
//
// OK let me just hardcode with screen coordinates directly.
// Screen: 0 deg = right, CLOCKWISE positive, Y-down.

static inline void gaugePoint(float cx, float cy, float r, float frac,
                               float startScreenDeg, float sweepScreenDeg,
                               float &outX, float &outY)
{
    // frac 0..1 maps to startScreenDeg .. startScreenDeg+sweepScreenDeg
    // Screen angle: 0=right, CW positive
    float angleDeg = startScreenDeg + frac * sweepScreenDeg;
    float angleRad = angleDeg * M_PI / 180.0f;
    outX = cx + r * std::cos(angleRad);
    outY = cy + r * std::sin(angleRad);
}

void SignalMeter::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    int w = width();
    int h = height();

    // Background
    p.fillRect(rect(), QColor(22, 22, 32));
    p.setPen(QPen(QColor(50, 60, 75), 1));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);

    // ══════════════════════════════════════
    // GEOMETRY
    // ══════════════════════════════════════
    float cx = w / 2.0f;
    float padTop = 10.0f;
    float padBottom = 30.0f;
    float padSide = 20.0f;

    float labelExtra = 50.0f;
    float maxR_sides = cx - padSide - labelExtra;
    float maxR_for_height = (h - padTop - padBottom - labelExtra) / 1.5f;
    float R = std::min(maxR_sides, maxR_for_height);
    if (R < 40.0f) R = 40.0f;

    float pivotY_min = R + labelExtra + padTop;
    float pivotY_max = h - padBottom - (R + labelExtra) * 0.5f;
    float pivotY_max2 = h - padBottom - R * 0.22f - 48.0f;
    float pivotY = std::min({pivotY_max, pivotY_max2, (pivotY_min + std::min(pivotY_max, pivotY_max2)) / 2.0f});
    if (pivotY < pivotY_min) pivotY = pivotY_min;

    float screenStart = 150.0f;
    float screenSweep = 240.0f;

    // ══════════════════════════════════════
    // COLORED ARC BAND (drawArc with thick pen)
    // ══════════════════════════════════════
    float bandW = 24.0f;
    QRectF arcRect(cx - R, pivotY - R, R * 2.0f, R * 2.0f);

    int colorSegs = 60;
    for (int i = 0; i < colorSegs; i++) {
        float f = static_cast<float>(i) / colorSegs;

        QColor c;
        if (f < 0.25f) {
            float t = f / 0.25f;
            c = QColor(0, 150 + static_cast<int>(t * 55), 0);
        } else if (f < 0.45f) {
            float t = (f - 0.25f) / 0.20f;
            c = QColor(static_cast<int>(t * 255), 200 - static_cast<int>(t * 10), 0);
        } else if (f < 0.65f) {
            float t = (f - 0.45f) / 0.20f;
            c = QColor(255, 190 - static_cast<int>(t * 110), 0);
        } else if (f < 0.85f) {
            float t = (f - 0.65f) / 0.20f;
            c = QColor(255, 80 - static_cast<int>(t * 50), 0);
        } else {
            float t = (f - 0.85f) / 0.15f;
            c = QColor(220 + static_cast<int>(t * 35), 30 - static_cast<int>(t * 30), 0);
        }

        float segAngleDeg = screenSweep / colorSegs;
        float segStartDeg = screenStart + f * screenSweep;

        // Qt drawArc: startAngle in 1/16 deg, from 3 o'clock, CCW positive
        // Our screen angle is from 3 o'clock CW positive
        // Qt angle = -screenAngle (negate to convert CW to CCW)
        int qtStart = static_cast<int>(-segStartDeg * 16.0f);
        int qtSweep = static_cast<int>(-(segAngleDeg + 0.3f) * 16.0f); // negative = CW

        p.setPen(QPen(c, bandW, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(arcRect, qtStart, qtSweep);
    }

    // ══════════════════════════════════════
    // TICKS + LABELS
    // ══════════════════════════════════════
    struct ScaleItem { float frac; const char* label; };
    ScaleItem items[] = {
        {0.000f, "0"},   {0.071f, "1"},   {0.143f, "2"},   {0.214f, "3"},
        {0.286f, "4"},   {0.357f, "5"},   {0.429f, "6"},   {0.500f, "7"},
        {0.571f, "8"},   {0.643f, "9"},   {0.750f, "+20"},  {0.875f, "+40"},
        {1.000f, "+60"},
    };
    int itemCount = 13;

    float tickOutR = R + bandW / 2.0f + 3.0f;
    float tickInR  = R - bandW / 2.0f - 3.0f;
    float labelR   = R + bandW / 2.0f + 18.0f;

    QFont labelFont("Arial", 10, QFont::Bold);
    p.setFont(labelFont);
    QFontMetrics lfm(labelFont);

    for (int i = 0; i < itemCount; i++) {
        float frac = items[i].frac;
        float screenAngle = screenStart + frac * screenSweep;
        float rad = screenAngle * M_PI / 180.0f;
        // Screen coords: cos for X, sin for Y (Y-down)
        float ca = std::cos(rad);
        float sa = std::sin(rad);

        bool isRed = (frac > 0.65f);

        // Tick
        p.setPen(QPen(isRed ? QColor(255, 130, 50) : QColor(195, 205, 220), 2.0f));
        p.drawLine(QPointF(cx + tickInR * ca, pivotY + tickInR * sa),
                   QPointF(cx + tickOutR * ca, pivotY + tickOutR * sa));

        // Label
        float lx = cx + labelR * ca;
        float ly = pivotY + labelR * sa;
        QString txt = items[i].label;
        p.setPen(isRed ? QColor(255, 150, 60) : QColor(205, 215, 230));
        p.drawText(QPointF(lx - lfm.horizontalAdvance(txt) / 2.0f,
                           ly + lfm.ascent() / 2.0f - 2.0f), txt);
    }

    // Minor ticks
    for (int i = 0; i < itemCount - 1; i++) {
        float midFrac = (items[i].frac + items[i + 1].frac) / 2.0f;
        float screenAngle = screenStart + midFrac * screenSweep;
        float rad = screenAngle * M_PI / 180.0f;
        float ca = std::cos(rad), sa = std::sin(rad);
        p.setPen(QPen(QColor(70, 80, 100), 1.0f));
        float minInR = R - bandW / 2.0f;
        p.drawLine(QPointF(cx + minInR * ca, pivotY + minInR * sa),
                   QPointF(cx + tickOutR * ca, pivotY + tickOutR * sa));
    }

    // ══════════════════════════════════════
    // NEEDLE
    // ══════════════════════════════════════
    float screenAngle = screenStart + m_displayLevel * screenSweep;
    float rad = screenAngle * M_PI / 180.0f;
    float nCos = std::cos(rad);
    float nSin = std::sin(rad);
    float needleLen = R - bandW / 2.0f - 8.0f;

    // Tapered polygon
    float bw = 4.0f;
    float pC = std::cos(rad + M_PI / 2.0f);
    float pS = std::sin(rad + M_PI / 2.0f);

    QPolygonF needlePoly;
    needlePoly << QPointF(cx + bw * pC, pivotY + bw * pS);
    needlePoly << QPointF(cx - bw * pC, pivotY - bw * pS);
    needlePoly << QPointF(cx + needleLen * nCos, pivotY + needleLen * nSin);

    QColor nCol = m_displayLevel < 0.65f ? QColor(225, 232, 242) : QColor(255, 95, 35);
    p.setBrush(nCol);
    p.setPen(QPen(QColor(0, 0, 0, 35), 0.5f));
    p.drawPolygon(needlePoly);

    // Peak hold
    if (m_peak > 0.01f && std::abs(m_peak - m_displayLevel) > 0.02f) {
        float pAngle = screenStart + m_peak * screenSweep;
        float pRad = pAngle * M_PI / 180.0f;
        float pkC = std::cos(pRad), pkS = std::sin(pRad);
        p.setPen(QPen(QColor(255, 255, 255, 65), 1.5f));
        p.drawLine(QPointF(cx + needleLen * 0.6f * pkC, pivotY + needleLen * 0.6f * pkS),
                   QPointF(cx + needleLen * pkC, pivotY + needleLen * pkS));
    }

    // ══════════════════════════════════════
    // PIVOT CIRCLE + dB / dBm
    // ══════════════════════════════════════
    float pivR = R * 0.22f;
    if (pivR < 16.0f) pivR = 16.0f;

    // Pivot circle color: blue for RX, red for TX
    if (m_txMode) {
        p.setBrush(QColor(180, 40, 30));
        p.setPen(QPen(QColor(220, 60, 40), 3));
    } else {
        p.setBrush(QColor(20, 100, 210));
        p.setPen(QPen(QColor(35, 135, 245), 3));
    }
    p.drawEllipse(QPointF(cx, pivotY), pivR, pivR);

    // Value in circle
    QString dbVal;
    if (m_txMode) {
        // TX: show estimated dBm directly
        int dbmInt = static_cast<int>(std::round(m_txDbm));
        dbVal = QString::number(dbmInt);
    } else {
        // RX: show relative dB from signal level
        float db = 20.0f * std::log10(std::max(m_displayLevel, 0.001f));
        int dbInt = static_cast<int>(std::round(db));
        dbVal = QString::number(dbInt);
    }

    int dbFontSize = static_cast<int>(pivR * 0.65f);
    if (dbFontSize < 8) dbFontSize = 8;
    QFont dbFont("Arial", dbFontSize, QFont::Bold);
    p.setFont(dbFont);
    QFontMetrics dfm(dbFont);
    p.setPen(Qt::white);
    p.drawText(QPointF(cx - dfm.horizontalAdvance(dbVal) / 2.0f,
                       pivotY + dfm.ascent() / 2.0f - 2.0f), dbVal);

    // Unit label below circle
    QFont unitFont("Arial", 10, QFont::Bold);
    p.setFont(unitFont);
    QFontMetrics ufm(unitFont);
    QString unitStr = m_txMode ? "dBm" : "dB";
    p.setPen(QColor(175, 185, 205));
    p.drawText(QPointF(cx - ufm.horizontalAdvance(unitStr) / 2.0f,
                       pivotY + pivR + 15.0f), unitStr);

    // S-unit (RX) or TX indicator
    QString bottomLabel;
    if (m_txMode) {
        bottomLabel = "TX";
    } else {
        float sVal = m_displayLevel * 9.0f;
        if (sVal < 0.5f) bottomLabel = "S0";
        else if (sVal <= 9.0f) bottomLabel = QString("S%1").arg(static_cast<int>(std::round(sVal)));
        else bottomLabel = QString("S9+%1").arg(static_cast<int>((sVal - 9.0f) * 10.0f));
    }

    QFont sFont("Arial", 9, QFont::Bold);
    p.setFont(sFont);
    QFontMetrics sfm(sFont);
    p.setPen(m_txMode ? QColor(255, 120, 80) : QColor(75, 195, 255));
    p.drawText(QPointF(cx - sfm.horizontalAdvance(bottomLabel) / 2.0f,
                       pivotY + pivR + 30.0f), bottomLabel);
}
