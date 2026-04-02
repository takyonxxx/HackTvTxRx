#include "glplotter.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFontMetrics>
#include <QToolTip>
#include <QDebug>
#include <cmath>
#include <algorithm>

#ifndef _MSC_VER
#include <sys/time.h>
#else
#include <Windows.h>
#include <cstdint>
static int gettimeofday(struct timeval *tp, struct timezone *)
{
    static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);
    SYSTEMTIME system_time;
    FILETIME file_time;
    uint64_t time;
    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);
    time = ((uint64_t)file_time.dwLowDateTime);
    time += ((uint64_t)file_time.dwHighDateTime) << 32;
    tp->tv_sec = (long)((time - EPOCH) / 10000000L);
    tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
    return 0;
}
#endif

#define COLPAL_MIX    0
#define DEFAULT_FREQ_STEP 5

#define PLOTTER_BGD_COLOR        QColor(4, 14, 22)
#define PLOTTER_GRID_COLOR       QColor(28, 105, 168, 90)
#define PLOTTER_TEXT_COLOR       QColor(180, 220, 180)
#define PLOTTER_CENTER_LINE_COL  QColor(255, 255, 0, 140)
#define PLOTTER_FILTER_LINE_COL  QColor(255, 80, 80, 180)
#define PLOTTER_FILTER_BOX_COL   QColor(0, 180, 255, 30)
#define WF_BG_COLOR              QColor(2, 8, 30)

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define FONT_WIDTH(metrics, text) metrics.horizontalAdvance(text)
#else
#define FONT_WIDTH(metrics, text) metrics.width(text)
#endif

static inline quint64 time_ms(void)
{
    struct timeval tval;
    gettimeofday(&tval, nullptr);
    return 1e3 * tval.tv_sec + 1e-3 * tval.tv_usec;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

CPlotter::CPlotter(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);

    // QPainter handles antialiasing internally, no need for MSAA
    // which can cause crashes on some GPU drivers

    m_FftColor = QColor(0xCE, 0xEC, 0xF5);
    m_FftFillCol = m_FftColor;
    m_FftFillCol.setAlpha(60);
    m_PeakHoldColor = m_FftColor;
    m_PeakHoldColor.setAlpha(60);

    setWaterfallPalette(COLPAL_MIX);
    memset(m_wfbuf, 255, MAX_SCREENSIZE);
    memset(m_fftbuf, 0, sizeof(m_fftbuf));
    memset(m_fftPeakHoldBuf, 0, sizeof(m_fftPeakHoldBuf));

    setStatusTip(tr("Click, drag or scroll on spectrum to tune. "
                    "Drag and scroll X and Y axes for pan and zoom. "
                    "Drag filter edges to adjust filter."));
}

CPlotter::~CPlotter()
{
}

QSize CPlotter::minimumSizeHint() const { return QSize(50, 50); }
QSize CPlotter::sizeHint() const { return QSize(400, 300); }

// ============================================================================
// OpenGL lifecycle
// ============================================================================

void CPlotter::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.031f, 0.078f, 1.0f);
}

void CPlotter::resizeGL(int w, int h)
{
    Q_UNUSED(w); Q_UNUSED(h);
    // Waterfall image management
    int wfH = (100 - m_Percent2DScreen) * h / 100;
    if (wfH < 1) wfH = 1;
    if (m_waterfallImg.isNull()) {
        m_waterfallImg = QImage(w, wfH, QImage::Format_RGB32);
        m_waterfallImg.fill(WF_BG_COLOR.rgb());
    } else {
        m_waterfallImg = m_waterfallImg.scaled(w, wfH, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    m_DrawOverlay = true;
}

// ============================================================================
// Main paint — QPainter on OpenGL for smooth antialiased rendering
// ============================================================================

void CPlotter::paintGL()
{
    const int w = width();
    const int h = height();
    if (w < 10 || h < 10) return;

    const int specH = m_Percent2DScreen * h / 100;
    const int wfH = h - specH;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // Background
    painter.fillRect(0, 0, w, specH, PLOTTER_BGD_COLOR);

    // Filter box (behind everything)
    if (m_FilterBoxEnabled)
        drawFilterBox(painter, w, specH);

    // Spectrum (fill + line)
    if (m_Running && m_fftData && m_fftDataSize > 0)
        drawSpectrum(painter, w, specH);

    // Grid + dB labels — drawn ON TOP of spectrum fill so always visible
    drawGrid(painter, w, specH);

    // Center line
    if (m_CenterLineEnabled) {
        int cx = xFromFreq(m_DemodCenterFreq);
        if (cx > 0 && cx < w) {
            QPen cpen(PLOTTER_CENTER_LINE_COL);
            cpen.setStyle(Qt::DashLine);
            painter.setPen(cpen);
            painter.drawLine(cx, 0, cx, specH);
        }
    }

    // Band allocation overlay at bottom of spectrum
    drawBandOverlay(painter, w, specH);

    // Waterfall
    drawWaterfall(painter, w, h);

    // Frequency labels at spectrum bottom
    drawFreqLabels(painter, w, specH);

    painter.end();
}

// ============================================================================
// Grid drawing
// ============================================================================

void CPlotter::drawGrid(QPainter &painter, int w, int h)
{
    QFontMetrics metrics(m_Font);
    m_YAxisWidth = FONT_WIDTH(metrics, "-100 ") + 4;
    int xAxisHeight = metrics.height() + 4;
    int plotW = w - m_YAxisWidth;
    int plotH = h - xAxisHeight;
    if (plotW < 10 || plotH < 10) return;

    QPen gridPen(PLOTTER_GRID_COLOR);
    gridPen.setStyle(Qt::SolidLine);
    gridPen.setWidthF(1.0);
    painter.setFont(m_Font);

    QColor gridCol = PLOTTER_GRID_COLOR;

    // Horizontal grid (dB) — draw as filled rects for reliable OpenGL rendering
    float pixPerDb = (float)plotH / fabs(m_PandMaxdB - m_PandMindB);
    float dbStep = 10.0f;
    if (pixPerDb * dbStep < 20) dbStep = 20.0f;
    if (pixPerDb * dbStep < 20) dbStep = 40.0f;

    for (float db = ceil(m_PandMindB / dbStep) * dbStep; db <= m_PandMaxdB; db += dbStep) {
        int y = (int)((m_PandMaxdB - db) * pixPerDb);
        if (y < 0 || y >= plotH) continue;
        painter.fillRect(m_YAxisWidth, y, plotW, 1, gridCol);
        painter.setPen(PLOTTER_TEXT_COLOR);
        painter.drawText(2, y + metrics.ascent() / 2, QString::number((int)db));
    }

    // Vertical grid (frequency) — draw as filled rects
    makeFrequencyStrs();
    for (int i = 0; i <= m_HorDivs; i++) {
        int x = m_YAxisWidth + (i * plotW / m_HorDivs);
        painter.fillRect(x, 0, 1, plotH, gridCol);
    }
}

// ============================================================================
// Spectrum curve drawing — smooth antialiased
// ============================================================================

void CPlotter::drawSpectrum(QPainter &painter, int w, int specH)
{
    std::lock_guard<std::mutex> lock(m_fftMutex);

    int xmin, xmax;
    int plotW = qMin(w, MAX_SCREENSIZE);

    getScreenIntegerFFTData(specH, plotW,
                            m_PandMaxdB, m_PandMindB,
                            m_FftCenter - (qint64)m_Span / 2,
                            m_FftCenter + (qint64)m_Span / 2,
                            m_fftData, m_fftbuf,
                            &xmin, &xmax);

    int n = xmax - xmin;
    if (n < 4) return;

    // Smooth the FFT data with a moving average filter
    // Use a temporary buffer to avoid modifying m_fftbuf (needed for waterfall)
    static qint32 smoothBuf[MAX_SCREENSIZE];
    const int kernelSize = 5; // 5-point moving average
    const int halfK = kernelSize / 2;
    for (int i = xmin; i < xmax; i++) {
        int sum = 0;
        int count = 0;
        for (int k = -halfK; k <= halfK; k++) {
            int idx = i + k;
            if (idx >= xmin && idx < xmax) {
                sum += m_fftbuf[idx];
                count++;
            }
        }
        smoothBuf[i] = sum / count;
    }

    // Build smooth cubic path using control points
    QPainterPath specPath;
    specPath.moveTo(xmin, smoothBuf[xmin]);

    // Downsample to control points, then use cubicTo for smooth curves
    const int step = qMax(2, n / 500); // ~500 control points max
    QVector<QPointF> pts;
    for (int i = xmin; i < xmax; i += step)
        pts.append(QPointF(i, smoothBuf[i]));
    // Always include last point
    if (pts.isEmpty() || pts.last().x() < xmax - 1)
        pts.append(QPointF(xmax - 1, smoothBuf[xmax - 1]));

    if (pts.size() >= 2) {
        specPath.moveTo(pts[0]);
        for (int i = 1; i < pts.size(); i++) {
            QPointF p0 = pts[i - 1];
            QPointF p1 = pts[i];
            float cx = (p0.x() + p1.x()) / 2.0f;
            specPath.cubicTo(cx, p0.y(), cx, p1.y(), p1.x(), p1.y());
        }
    }

    // Fill under curve — SDRuno style: solid opaque white→blue gradient fill
    if (m_FftFill) {
        QPainterPath fillPath = specPath;
        fillPath.lineTo(xmax - 1, specH);
        fillPath.lineTo(xmin, specH);
        fillPath.closeSubpath();

        QLinearGradient grad(0, 0, 0, specH);
        grad.setColorAt(0.0,  QColor(245, 250, 255, 255));  // pure white at peaks
        grad.setColorAt(0.02, QColor(200, 225, 255, 255));  // white-blue
        grad.setColorAt(0.08, QColor(120, 180, 255, 255));  // light blue
        grad.setColorAt(0.20, QColor(50, 120, 240, 255));   // sharp to medium blue
        grad.setColorAt(0.40, QColor(20, 70, 200, 255));    // deep blue
        grad.setColorAt(0.60, QColor(8, 40, 150, 255));     // dark blue
        grad.setColorAt(0.80, QColor(3, 18, 80, 255));      // very dark navy
        grad.setColorAt(1.0,  QColor(1, 6, 35, 255));       // almost black at bottom

        painter.setPen(Qt::NoPen);
        painter.setBrush(grad);
        painter.drawPath(fillPath);
    }

    // Draw spectrum line — white like SDRuno
    // Subtle glow under the white line
    QPen glowPen(QColor(180, 210, 255, 50));
    glowPen.setWidthF(3.0);
    painter.setPen(glowPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(specPath);

    // Main white line
    QPen mainPen(QColor(240, 248, 255));
    mainPen.setWidthF(1.2);
    painter.setPen(mainPen);
    painter.drawPath(specPath);

    // Peak hold
    if (m_PeakHoldActive) {
        for (int i = xmin; i < xmax; i++) {
            if (!m_PeakHoldValid || m_fftbuf[i] < m_fftPeakHoldBuf[i])
                m_fftPeakHoldBuf[i] = m_fftbuf[i];
        }
        QPainterPath peakPath;
        peakPath.moveTo(xmin, m_fftPeakHoldBuf[xmin]);
        for (int i = xmin + 1; i < xmax; i++)
            peakPath.lineTo(i, m_fftPeakHoldBuf[i]);

        QPen peakPen(m_PeakHoldColor);
        peakPen.setWidthF(0.8);
        peakPen.setStyle(Qt::DotLine);
        painter.setPen(peakPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(peakPath);
        m_PeakHoldValid = true;
    }
}

// ============================================================================
// Waterfall — QImage based, drawn once per update, scrolled on GPU via QPainter
// ============================================================================

void CPlotter::drawWaterfall(QPainter &painter, int w, int h)
{
    int specH = m_Percent2DScreen * h / 100;
    int wfH = h - specH;
    if (wfH < 2 || w < 2) return;

    // Update waterfall data if running
    if (m_Running && m_fftData && m_fftDataSize > 0) {
        std::lock_guard<std::mutex> lock(m_fftMutex);
        int xmin, xmax;
        int n = qMin(w, MAX_SCREENSIZE);
        getScreenIntegerFFTData(255, n, m_WfMaxdB, m_WfMindB,
                                m_FftCenter - (qint64)m_Span / 2,
                                m_FftCenter + (qint64)m_Span / 2,
                                m_wfData, m_fftbuf,
                                &xmin, &xmax);

        if (msec_per_wfline > 0) {
            for (int i = 0; i < n; i++) {
                if (m_fftbuf[i] < m_wfbuf[i])
                    m_wfbuf[i] = m_fftbuf[i];
            }
        }

        quint64 tnow = time_ms();
        if (tnow - tlast_wf_ms >= msec_per_wfline) {
            tlast_wf_ms = tnow;

            // Ensure waterfall image matches current size
            if (m_waterfallImg.width() != w || m_waterfallImg.height() != wfH) {
                m_waterfallImg = m_waterfallImg.scaled(w, wfH, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            }

            // Scroll down by 1 line
            if (m_waterfallImg.height() > 1) {
                memmove(m_waterfallImg.scanLine(1),
                        m_waterfallImg.scanLine(0),
                        (m_waterfallImg.height() - 1) * m_waterfallImg.bytesPerLine());
            }

            // Draw new top line with smoothing for soft appearance
            QRgb *line = reinterpret_cast<QRgb*>(m_waterfallImg.scanLine(0));

            // Smooth the waterfall data with 3-point average
            static qint32 wfSmooth[MAX_SCREENSIZE];
            for (int i = 0; i < w; i++) {
                if (i >= xmin && i < xmax) {
                    int val = msec_per_wfline > 0 ? m_wfbuf[i] : m_fftbuf[i];
                    int prev = (i > xmin) ? (msec_per_wfline > 0 ? m_wfbuf[i-1] : m_fftbuf[i-1]) : val;
                    int next = (i < xmax-1) ? (msec_per_wfline > 0 ? m_wfbuf[i+1] : m_fftbuf[i+1]) : val;
                    wfSmooth[i] = (prev + val * 2 + next) / 4;
                } else {
                    wfSmooth[i] = 255;
                }
            }

            for (int i = 0; i < w; i++) {
                if (i >= xmin && i < xmax) {
                    int idx = 255 - wfSmooth[i];
                    idx = qBound(0, idx, 255);
                    line[i] = m_ColorTbl[idx].rgb();
                    if (msec_per_wfline > 0)
                        m_wfbuf[i] = 255;
                } else {
                    line[i] = WF_BG_COLOR.rgb();
                }
            }
        }
    }

    // Draw the waterfall image
    painter.drawImage(0, specH, m_waterfallImg);

    // Draw a thin separator line
    QPen sepPen(QColor(0, 180, 255, 120));
    sepPen.setWidthF(1.0);
    painter.setPen(sepPen);
    painter.drawLine(0, specH, w, specH);
}

// ============================================================================
// Filter box
// ============================================================================

void CPlotter::drawFilterBox(QPainter &painter, int w, int h)
{
    int lo = xFromFreq(m_DemodCenterFreq + m_DemodLowCutFreq);
    int hi = xFromFreq(m_DemodCenterFreq + m_DemodHiCutFreq);
    if (lo > hi) std::swap(lo, hi);
    lo = qBound(0, lo, w);
    hi = qBound(0, hi, w);
    if (hi - lo < 2) return;

    // Filter shading
    QLinearGradient fgrad(0, 0, 0, h);
    fgrad.setColorAt(0.0, QColor(0, 180, 255, 35));
    fgrad.setColorAt(1.0, QColor(0, 100, 200, 10));
    painter.fillRect(lo, 0, hi - lo, h, fgrad);

    // Filter edges
    QPen edgePen(PLOTTER_FILTER_LINE_COL);
    edgePen.setWidthF(1.0);
    painter.setPen(edgePen);
    painter.drawLine(lo, 0, lo, h);
    painter.drawLine(hi, 0, hi, h);
}

// ============================================================================
// Band allocation overlay — known frequency bands shown at bottom of spectrum
// ============================================================================

void CPlotter::drawBandOverlay(QPainter &painter, int w, int specH)
{
    struct BandInfo {
        qint64 startHz;
        qint64 endHz;
        const char *name;
        QColor color;
    };

    static const BandInfo bands[] = {
        // LF / MF
        {148500,    283500,     "LW",               QColor(80, 80, 180)},
        {526500,    1606500,    "MW",                QColor(80, 100, 160)},
        // HF
        {1800000,   2000000,    "160m Ham",          QColor(180, 60, 60)},
        {2300000,   2495000,    "120m SW",           QColor(60, 130, 60)},
        {3200000,   3400000,    "90m SW",            QColor(60, 130, 60)},
        {3500000,   3800000,    "80m Ham",           QColor(180, 60, 60)},
        {3900000,   4000000,    "75m SW",            QColor(60, 130, 60)},
        {4750000,   5060000,    "60m SW",            QColor(60, 130, 60)},
        {5351500,   5366500,    "60m Ham",           QColor(180, 60, 60)},
        {5900000,   6200000,    "49m SW",            QColor(60, 130, 60)},
        {7000000,   7200000,    "40m Ham",           QColor(180, 60, 60)},
        {7200000,   7450000,    "41m SW",            QColor(60, 130, 60)},
        {9400000,   9900000,    "31m SW",            QColor(60, 130, 60)},
        {10100000,  10150000,   "30m Ham",           QColor(180, 60, 60)},
        {11600000,  12100000,   "25m SW",            QColor(60, 130, 60)},
        {13570000,  13870000,   "22m SW",            QColor(60, 130, 60)},
        {14000000,  14350000,   "20m Ham",           QColor(180, 60, 60)},
        {15100000,  15800000,   "19m SW",            QColor(60, 130, 60)},
        {17480000,  17900000,   "16m SW",            QColor(60, 130, 60)},
        {18068000,  18168000,   "17m Ham",           QColor(180, 60, 60)},
        {18900000,  19020000,   "15m SW",            QColor(60, 130, 60)},
        {21000000,  21450000,   "15m Ham",           QColor(180, 60, 60)},
        {21450000,  21850000,   "13m SW",            QColor(60, 130, 60)},
        {24890000,  24990000,   "12m Ham",           QColor(180, 60, 60)},
        {25670000,  26100000,   "11m SW",            QColor(60, 130, 60)},
        {26965000,  27405000,   "CB",                QColor(180, 140, 40)},
        {28000000,  29700000,   "10m Ham",           QColor(180, 60, 60)},
        {50000000,  54000000,   "6m Ham",            QColor(180, 60, 60)},
        // VHF
        {64000000,  68000000,   "TV Ch1-3",          QColor(100, 100, 160)},
        {76000000,  87500000,   "TV Ch4-5 / JP FM",  QColor(100, 100, 160)},
        {87500000,  108000000,  "FM Broadcast",      QColor(40, 100, 200)},
        {108000000, 117975000,  "Air Nav",           QColor(160, 130, 40)},
        {118000000, 136975000,  "Air Voice",         QColor(160, 160, 40)},
        {144000000, 148000000,  "2m Ham",            QColor(180, 60, 60)},
        {156000000, 162025000,  "Marine VHF",        QColor(40, 140, 160)},
        {162400000, 162550000,  "NOAA Weather",      QColor(60, 160, 100)},
        {174000000, 230000000,  "VHF TV",            QColor(100, 100, 160)},
        {230000000, 240000000,  "DAB",               QColor(130, 80, 180)},
        // UHF
        {400000000, 406000000,  "Meteorological",    QColor(60, 150, 130)},
        {420000000, 450000000,  "70cm Ham",          QColor(180, 60, 60)},
        {462562500, 467712500,  "GMRS/FRS",          QColor(180, 140, 40)},
        {470000000, 698000000,  "UHF TV",            QColor(100, 100, 160)},
        {698000000, 806000000,  "LTE 700",           QColor(130, 80, 180)},
        {824000000, 849000000,  "Cell 850 Up",       QColor(130, 80, 180)},
        {869000000, 894000000,  "Cell 850 Down",     QColor(130, 80, 180)},
        {902000000, 928000000,  "33cm Ham / ISM",    QColor(180, 60, 60)},
        {935000000, 960000000,  "GSM 900 Down",      QColor(130, 80, 180)},
        {1240000000,1300000000, "23cm Ham",          QColor(180, 60, 60)},
        {1525000000,1559000000, "GPS L1/Inmarsat",   QColor(60, 150, 130)},
        {1710000000,1785000000, "AWS Up",            QColor(130, 80, 180)},
        {1805000000,1880000000, "GSM 1800 Down",     QColor(130, 80, 180)},
        {1920000000,1980000000, "UMTS Up",           QColor(130, 80, 180)},
        {2110000000,2170000000, "UMTS Down",         QColor(130, 80, 180)},
        {2400000000LL,2483500000LL, "ISM 2.4G/WiFi", QColor(180, 140, 40)},
    };

    const int numBands = sizeof(bands) / sizeof(bands[0]);

    qint64 viewStart = m_CenterFreq + m_FftCenter - m_Span / 2;
    qint64 viewEnd = viewStart + m_Span;

    QFontMetrics fm(m_Font);
    int freqLabelH = fm.height() + 6;
    int bandBarH = 24;
    int bandBarY = specH - freqLabelH - bandBarH;

    QFont bandFont = m_Font;
    bandFont.setPixelSize(12);
    bandFont.setBold(true);
    QFontMetrics bm(bandFont);
    painter.setFont(bandFont);

    for (int b = 0; b < numBands; b++) {
        const BandInfo &band = bands[b];

        // Skip if band is completely outside view
        if (band.endHz <= viewStart || band.startHz >= viewEnd)
            continue;

        int x1 = xFromFreq(qMax(band.startHz, viewStart));
        int x2 = xFromFreq(qMin(band.endHz, viewEnd));
        int bw = x2 - x1;
        if (bw < 3) continue;

        // Band color bar
        QColor barCol = band.color;
        barCol.setAlpha(140);
        painter.fillRect(x1, bandBarY, bw, bandBarH, barCol);

        // Border
        QColor borderCol = band.color;
        borderCol.setAlpha(200);
        painter.setPen(borderCol);
        painter.drawRect(x1, bandBarY, bw, bandBarH);

        // Label if it fits
        int textW = FONT_WIDTH(bm, band.name);
        if (bw > textW + 6) {
            painter.setPen(QColor(230, 230, 230));
            QRect textRect(x1 + 2, bandBarY, bw - 4, bandBarH);
            painter.drawText(textRect, Qt::AlignCenter, band.name);
        }
    }
}

// ============================================================================
// Frequency labels
// ============================================================================

void CPlotter::drawFreqLabels(QPainter &painter, int w, int specH)
{
    QFontMetrics metrics(m_Font);
    int plotW = w - m_YAxisWidth;
    if (plotW < 10) return;

    painter.setPen(PLOTTER_TEXT_COLOR);
    painter.setFont(m_Font);

    for (int i = 0; i <= m_HorDivs; i++) {
        int x = m_YAxisWidth + (i * plotW / m_HorDivs);
        QRect textRect(x - 40, specH - metrics.height() - 2, 80, metrics.height());
        if (i < HORZ_DIVS_MAX + 1)
            painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignBottom, m_HDivText[i]);
    }

    // "MHz" label at right edge
    QRect unitRect(w - 35, specH - metrics.height() - 2, 35, metrics.height());
    painter.drawText(unitRect, Qt::AlignRight | Qt::AlignBottom, tr("MHz"));
}

// ============================================================================
// FFT data interface — thread safe with internal copy
// ============================================================================

void CPlotter::setNewFttData(float *fftData, int size)
{
    if (!m_Running) m_Running = true;
    {
        std::lock_guard<std::mutex> lock(m_fftMutex);
        m_fftDataBuf.assign(fftData, fftData + size);
        m_wfDataBuf.assign(fftData, fftData + size);
        m_fftData = m_fftDataBuf.data();
        m_wfData = m_wfDataBuf.data();
        m_fftDataSize = size;
    }
    update();
}

void CPlotter::setNewFttData(float *fftData, float *wfData, int size)
{
    if (!m_Running) m_Running = true;
    {
        std::lock_guard<std::mutex> lock(m_fftMutex);
        m_fftDataBuf.assign(fftData, fftData + size);
        m_wfDataBuf.assign(wfData, wfData + size);
        m_fftData = m_fftDataBuf.data();
        m_wfData = m_wfDataBuf.data();
        m_fftDataSize = size;
    }
    update();
}

void CPlotter::draw()
{
    update(); // Just trigger paintGL
}

// ============================================================================
// Coordinate transforms (kept from original CPlotter)
// ============================================================================

int CPlotter::xFromFreq(qint64 freq)
{
    int w = width();
    QFontMetrics metrics(m_Font);
    int yAxisW = FONT_WIDTH(metrics, "-100 ") + 4;
    int plotW = w - yAxisW;
    qint64 startFreq = m_CenterFreq + m_FftCenter - m_Span / 2;
    int x = yAxisW + (int)((float)plotW * ((float)(freq - startFreq) / (float)m_Span));
    return qBound(0, x, w);
}

qint64 CPlotter::freqFromX(int x)
{
    int w = width();
    QFontMetrics metrics(m_Font);
    int yAxisW = FONT_WIDTH(metrics, "-100 ") + 4;
    int plotW = w - yAxisW;
    if (plotW <= 0) return m_CenterFreq;
    qint64 startFreq = m_CenterFreq + m_FftCenter - m_Span / 2;
    return startFreq + (qint64)((float)m_Span * (float)(x - yAxisW) / (float)plotW);
}

// ============================================================================
// Mouse interaction (simplified from original)
// ============================================================================

void CPlotter::mouseMoveEvent(QMouseEvent *event)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QPoint pt = event->pos();
#else
    QPoint pt = event->position().toPoint();
#endif

    int specH = m_Percent2DScreen * height() / 100;

    // Update screen positions of filter edges
    m_DemodFreqX = xFromFreq(m_DemodCenterFreq);
    m_DemodLowCutFreqX = xFromFreq(m_DemodCenterFreq + m_DemodLowCutFreq);
    m_DemodHiCutFreqX = xFromFreq(m_DemodCenterFreq + m_DemodHiCutFreq);

    if (event->buttons() & (Qt::LeftButton | Qt::RightButton))
    {
        if (m_CursorCaptured == CENTER) {
            qint64 freq = freqFromX(pt.x() - m_GrabPosition);
            freq = roundFreq(freq, m_ClickResolution);
            m_DemodCenterFreq = freq;
            clampDemodParameters();
            emit newDemodFreq(m_DemodCenterFreq, m_DemodCenterFreq - m_CenterFreq);
            update();
            return;
        }

        if (m_CursorCaptured == LEFT) {
            m_DemodLowCutFreq = freqFromX(pt.x()) - m_DemodCenterFreq;
            m_DemodLowCutFreq = roundFreq(m_DemodLowCutFreq, m_FilterClickResolution);
            if (m_symetric && (event->buttons() & Qt::LeftButton))
                m_DemodHiCutFreq = -m_DemodLowCutFreq;
            clampDemodParameters();
            emit newFilterFreq(m_DemodLowCutFreq, m_DemodHiCutFreq);
            update();
            return;
        }

        if (m_CursorCaptured == RIGHT) {
            m_DemodHiCutFreq = freqFromX(pt.x()) - m_DemodCenterFreq;
            m_DemodHiCutFreq = roundFreq(m_DemodHiCutFreq, m_FilterClickResolution);
            if (m_symetric && (event->buttons() & Qt::LeftButton))
                m_DemodLowCutFreq = -m_DemodHiCutFreq;
            clampDemodParameters();
            emit newFilterFreq(m_DemodLowCutFreq, m_DemodHiCutFreq);
            update();
            return;
        }

        if (m_CursorCaptured == YAXIS) {
            float delta = (float)(m_Yzero - pt.y()) / (float)specH;
            float range = m_PandMaxdB - m_PandMindB;
            m_PandMindB -= delta * range * 0.1f;
            m_PandMaxdB -= delta * range * 0.1f;
            m_Yzero = pt.y();
            emit pandapterRangeChanged(m_PandMindB, m_PandMaxdB);
            update();
            return;
        }
    }
    else
    {
        // No buttons pressed — update cursor shape based on hover position
        if (isPointCloseTo(pt.x(), m_DemodLowCutFreqX, m_CursorCaptureDelta)) {
            setCursor(Qt::SizeHorCursor);
            m_CursorCaptured = LEFT;
        } else if (isPointCloseTo(pt.x(), m_DemodHiCutFreqX, m_CursorCaptureDelta)) {
            setCursor(Qt::SizeHorCursor);
            m_CursorCaptured = RIGHT;
        } else if (isPointCloseTo(pt.x(), m_DemodFreqX, m_CursorCaptureDelta + 5)) {
            setCursor(Qt::SizeAllCursor);
            m_CursorCaptured = CENTER;
        } else if (pt.x() < m_YAxisWidth) {
            setCursor(Qt::OpenHandCursor);
            m_CursorCaptured = YAXIS;
        } else {
            setCursor(Qt::CrossCursor);
            m_CursorCaptured = NOCAP;
            m_GrabPosition = 0;
        }
    }

    // Tooltip
    if (m_TooltipsEnabled && pt.y() < specH) {
        qint64 freq = freqFromX(pt.x());
        QToolTip::showText(mapToGlobal(pt),
                           QString("%1 MHz").arg((double)freq / 1e6, 0, 'f', 4));
    }
}

void CPlotter::mousePressEvent(QMouseEvent *event)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QPoint pt = event->pos();
#else
    QPoint pt = event->position().toPoint();
#endif

    if (event->button() == Qt::LeftButton) {
        m_DemodFreqX = xFromFreq(m_DemodCenterFreq);
        m_DemodLowCutFreqX = xFromFreq(m_DemodCenterFreq + m_DemodLowCutFreq);
        m_DemodHiCutFreqX = xFromFreq(m_DemodCenterFreq + m_DemodHiCutFreq);

        if (isPointCloseTo(pt.x(), m_DemodLowCutFreqX, m_CursorCaptureDelta)) {
            m_CursorCaptured = LEFT;
            m_GrabPosition = 0;
        } else if (isPointCloseTo(pt.x(), m_DemodHiCutFreqX, m_CursorCaptureDelta)) {
            m_CursorCaptured = RIGHT;
            m_GrabPosition = 0;
        } else if (isPointCloseTo(pt.x(), m_DemodFreqX, m_CursorCaptureDelta + 5)) {
            m_CursorCaptured = CENTER;
            m_GrabPosition = pt.x() - m_DemodFreqX;
        } else if (pt.x() < m_YAxisWidth) {
            m_CursorCaptured = YAXIS;
            m_Yzero = pt.y();
        } else {
            // Click to tune
            qint64 freq = freqFromX(pt.x());
            freq = roundFreq(freq, m_ClickResolution);
            m_DemodCenterFreq = freq;
            clampDemodParameters();
            emit newDemodFreq(m_DemodCenterFreq, m_DemodCenterFreq - m_CenterFreq);
            m_CursorCaptured = CENTER;
            m_GrabPosition = 0;
            update();
        }
    }
}

void CPlotter::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    m_CursorCaptured = NOCAP;
    setCursor(Qt::CrossCursor);
}

void CPlotter::wheelEvent(QWheelEvent *event)
{
    int delta = m_InvertScrolling ? -event->angleDelta().y() : event->angleDelta().y();
    if (delta != 0)
        emit wheelFreqChange(delta > 0 ? 1 : -1);
}

// ============================================================================
// Helper methods (from original CPlotter, adapted)
// ============================================================================

void CPlotter::setCenterFreq(quint64 f)
{
    if ((quint64)m_CenterFreq == f) return;
    m_CenterFreq = f;
    m_DemodCenterFreq = m_CenterFreq;
    m_PeakHoldValid = false;
    m_DrawOverlay = true;
    update();
}

void CPlotter::setDemodRanges(int FLowCmin, int FLowCmax, int FHiCmin, int FHiCmax, bool symetric)
{
    m_FLowCmin = FLowCmin;
    m_FLowCmax = FLowCmax;
    m_FHiCmin = FHiCmin;
    m_FHiCmax = FHiCmax;
    m_symetric = symetric;
    clampDemodParameters();
}

void CPlotter::clampDemodParameters()
{
    if (m_symetric) {
        if (m_DemodLowCutFreq < m_FLowCmin) m_DemodLowCutFreq = m_FLowCmin;
        if (m_DemodLowCutFreq > m_FLowCmax) m_DemodLowCutFreq = m_FLowCmax;
        m_DemodHiCutFreq = -m_DemodLowCutFreq;
    } else {
        if (m_DemodLowCutFreq < m_FLowCmin) m_DemodLowCutFreq = m_FLowCmin;
        if (m_DemodLowCutFreq > m_FLowCmax) m_DemodLowCutFreq = m_FLowCmax;
        if (m_DemodHiCutFreq < m_FHiCmin) m_DemodHiCutFreq = m_FHiCmin;
        if (m_DemodHiCutFreq > m_FHiCmax) m_DemodHiCutFreq = m_FHiCmax;
    }
}

qint64 CPlotter::roundFreq(qint64 freq, int resolution)
{
    return resolution > 0 ? (freq / resolution) * resolution : freq;
}

void CPlotter::zoomStepX(float factor, int x)
{
    qint64 freq = freqFromX(x);
    qint64 newSpan = qBound((qint64)1000, (qint64)(m_Span * factor), (qint64)m_SampleFreq);
    m_Span = newSpan;
    qint64 newCenter = freq - (qint64)((float)(x - m_YAxisWidth) / (float)(width() - m_YAxisWidth) * m_Span);
    setFftCenterFreq(newCenter + m_Span / 2 - m_CenterFreq);
    emit newZoomLevel(1.0f);
    m_DrawOverlay = true;
}

void CPlotter::makeFrequencyStrs()
{
    int plotW = width() - m_YAxisWidth;
    if (plotW <= 0) return;

    qint64 startFreq = m_CenterFreq + m_FftCenter - m_Span / 2;
    qint64 freqPerDiv = m_Span / m_HorDivs;

    float unitScale = 1.0f;
    if (m_FreqUnits == 1000000) unitScale = 1e6;
    else if (m_FreqUnits == 1000) unitScale = 1e3;

    for (int i = 0; i <= m_HorDivs; i++) {
        qint64 f = startFreq + i * freqPerDiv;
        m_HDivText[i] = QString::number((double)f / unitScale, 'f', m_FreqDigits);
    }
}

void CPlotter::getScreenIntegerFFTData(qint32 plotHeight, qint32 plotWidth,
                                        float maxdB, float mindB,
                                        qint64 startFreq, qint64 stopFreq,
                                        float *inBuf, qint32 *outBuf,
                                        int *xmin, int *xmax)
{
    qint32 i, y, x;
    qint32 ymax = 10000;
    qint32 xprev = -1;
    qint32 m_FFTSize = m_fftDataSize;
    float dBGainFactor = ((float)plotHeight) / fabs(maxdB - mindB);

    qint32 m_BinMin = (qint32)((float)startFreq * (float)m_FFTSize / m_SampleFreq) + (m_FFTSize / 2);
    qint32 m_BinMax = (qint32)((float)stopFreq * (float)m_FFTSize / m_SampleFreq) + (m_FFTSize / 2);

    qint32 minbin = qMax(0, m_BinMin);
    if (m_BinMax <= m_BinMin) m_BinMax = m_BinMin + 1;
    qint32 maxbin = qMin(m_BinMax, m_FFTSize);

    bool largeFft = (m_BinMax - m_BinMin) > plotWidth;
    std::vector<qint32> translateTbl(qMax(m_FFTSize, plotWidth));

    if (largeFft) {
        for (i = minbin; i < maxbin; i++)
            translateTbl[i] = ((qint64)(i - m_BinMin) * plotWidth) / (m_BinMax - m_BinMin);
        *xmin = translateTbl[minbin];
        *xmax = translateTbl[maxbin - 1];
    } else {
        for (i = 0; i < plotWidth; i++)
            translateTbl[i] = m_BinMin + (i * (m_BinMax - m_BinMin)) / plotWidth;
        *xmin = 0;
        *xmax = plotWidth;
    }

    if (largeFft) {
        for (i = minbin; i < maxbin; i++) {
            if (i < 0 || i >= m_FFTSize) continue;
            y = (qint32)(dBGainFactor * (maxdB - inBuf[i]));
            y = qBound(0, y, plotHeight);
            x = translateTbl[i];
            if (x == xprev) {
                if (y < ymax) { outBuf[x] = y; ymax = y; }
            } else {
                outBuf[x] = y; xprev = x; ymax = y;
            }
        }
    } else {
        for (x = 0; x < plotWidth; x++) {
            i = translateTbl[x];
            if (i < 0 || i >= m_FFTSize)
                y = plotHeight;
            else
                y = (qint32)(dBGainFactor * (maxdB - inBuf[i]));
            y = qBound(0, y, plotHeight);
            outBuf[x] = y;
        }
    }
}

void CPlotter::calcDivSize(qint64 low, qint64 high, int divswanted, qint64 &adjlow, qint64 &step, int &divs)
{
    qint64 range = high - low;
    step = range / divswanted;
    if (step == 0) step = 1;
    qint64 mag = 1;
    while (mag * 10 <= step) mag *= 10;
    if (step / mag >= 5) step = 5 * mag;
    else if (step / mag >= 2) step = 2 * mag;
    else step = mag;
    adjlow = (low / step) * step;
    divs = (int)((high - adjlow) / step);
}

// ============================================================================
// Public slots (settings)
// ============================================================================

void CPlotter::setFftPlotColor(const QColor color)
{
    m_FftColor = color;
    m_FftFillCol = color;
    m_FftFillCol.setAlpha(60);
    m_PeakHoldColor = color;
    m_PeakHoldColor.setAlpha(60);
}

void CPlotter::setFftFill(bool enabled) { m_FftFill = enabled; }
void CPlotter::setPeakHold(bool enabled) { m_PeakHoldActive = enabled; m_PeakHoldValid = false; }

void CPlotter::setFftRange(float min, float max)
{
    setWaterfallRange(min, max);
    setPandapterRange(min, max);
}

void CPlotter::setPandapterRange(float min, float max)
{
    if (min < max) { m_PandMindB = min; m_PandMaxdB = max; }
    m_DrawOverlay = true;
    update();
}

void CPlotter::setWaterfallRange(float min, float max)
{
    if (min < max) { m_WfMindB = min; m_WfMaxdB = max; }
}

void CPlotter::setPeakDetection(bool enabled, float c)
{
    m_PeakDetection = enabled ? c : 0;
    m_Peaks.clear();
}

void CPlotter::updateOverlay()
{
    m_DrawOverlay = true;
    update();
}

void CPlotter::resetHorizontalZoom(void)
{
    setFftCenterFreq(0);
    setSpanFreq((qint32)m_SampleFreq);
}

void CPlotter::moveToCenterFreq(void)
{
    setFftCenterFreq(0);
    m_DrawOverlay = true;
    update();
}

void CPlotter::moveToDemodFreq(void)
{
    setFftCenterFreq(m_DemodCenterFreq - m_CenterFreq);
    m_DrawOverlay = true;
    update();
}

void CPlotter::zoomOnXAxis(float level)
{
    float factor = 1.0f / level;
    qint64 newSpan = qBound((qint64)1000, (qint64)(m_SampleFreq * factor), (qint64)m_SampleFreq);
    m_Span = newSpan;
    m_DrawOverlay = true;
    update();
}

void CPlotter::setFreqStep(unsigned int newFreqStep) { freqStep = newFreqStep; }

int CPlotter::getNearestPeak(QPoint pt)
{
    Q_UNUSED(pt);
    return 0;
}

void CPlotter::setWaterfallSpan(quint64 span_ms)
{
    wf_span = span_ms;
    int wfH = (100 - m_Percent2DScreen) * height() / 100;
    if (wfH > 0 && wf_span > 0)
        msec_per_wfline = wf_span / wfH;
    else
        msec_per_wfline = 0;
}

quint64 CPlotter::getWfTimeRes(void)
{
    return msec_per_wfline;
}

void CPlotter::setFftRate(int rate_hz)
{
    fft_rate = rate_hz;
    int wfH = (100 - m_Percent2DScreen) * height() / 100;
    if (wf_span == 0 && wfH > 0 && fft_rate > 0)
        msec_per_wfline = 0;
}

void CPlotter::clearWaterfall(void)
{
    if (!m_waterfallImg.isNull())
        m_waterfallImg.fill(WF_BG_COLOR.rgb());
    memset(m_wfbuf, 255, MAX_SCREENSIZE);
}

bool CPlotter::saveWaterfall(const QString &filename) const
{
    return m_waterfallImg.save(filename);
}

quint64 CPlotter::msecFromY(int y)
{
    Q_UNUSED(y);
    return 0;
}

// ============================================================================
// Waterfall palette (from original)
// ============================================================================

void CPlotter::setWaterfallPalette(int pal)
{
    Q_UNUSED(pal);
    // SDR# style: noise floor = deep dark blue, signals pop as cyan→yellow→red
    for (int i = 0; i < 256; i++) {
        float t;
        if (i < 50) {
            // Noise floor: near-black → deep dark blue (dominant background color)
            t = (float)i / 50.0f;
            m_ColorTbl[i].setRgb(0, 0, (int)(8 + 40 * t));
        } else if (i < 90) {
            // Dark blue → medium blue
            t = (float)(i - 50) / 40.0f;
            m_ColorTbl[i].setRgb(0, (int)(12 * t), (int)(48 + 75 * t));
        } else if (i < 130) {
            // Medium blue → bright blue
            t = (float)(i - 90) / 40.0f;
            m_ColorTbl[i].setRgb((int)(10 * t), (int)(12 + 80 * t), (int)(123 + 90 * t));
        } else if (i < 160) {
            // Bright blue → cyan
            t = (float)(i - 130) / 30.0f;
            m_ColorTbl[i].setRgb((int)(10 + 45 * t), (int)(92 + 135 * t), (int)(213 + 42 * t));
        } else if (i < 190) {
            // Cyan → yellow
            t = (float)(i - 160) / 30.0f;
            m_ColorTbl[i].setRgb((int)(55 + 200 * t), (int)(227 + 28 * t), (int)(255 - 225 * t));
        } else if (i < 225) {
            // Yellow → orange-red
            t = (float)(i - 190) / 35.0f;
            m_ColorTbl[i].setRgb(255, (int)(255 - 160 * t), (int)(30 - 30 * t));
        } else {
            // Orange-red → deep red
            t = (float)(i - 225) / 30.0f;
            if (t > 1.0f) t = 1.0f;
            m_ColorTbl[i].setRgb(255, (int)(95 - 95 * t), 0);
        }
    }
}
