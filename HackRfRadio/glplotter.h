#ifndef GLPLOTTER_H
#define GLPLOTTER_H

#include <QWidget>
#include <QTimer>
#include <QFont>
#include <QMap>
#include <QList>
#include <QPair>
#include <QRect>
#include <QMouseEvent>
#include <QWheelEvent>
#include <vector>
#include <mutex>

#define HORZ_DIVS_MAX 12
#define VERT_DIVS_MIN 5
#define MAX_SCREENSIZE 16384

#define PEAK_CLICK_MAX_H_DISTANCE   10
#define PEAK_CLICK_MAX_V_DISTANCE   20
#define PEAK_H_TOLERANCE            2

class CPlotter : public QWidget
{
    Q_OBJECT

public:
    explicit CPlotter(QWidget *parent = nullptr);
    ~CPlotter();

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

    void draw();
    void setRunningState(bool running) { m_Running = running; }
    void setClickResolution(int clickres) { m_ClickResolution = clickres; }
    void setFilterClickResolution(int clickres) { m_FilterClickResolution = clickres; }
    void setFilterBoxEnabled(bool enabled) { m_FilterBoxEnabled = enabled; }
    void setCenterLineEnabled(bool enabled) { m_CenterLineEnabled = enabled; }
    void setTooltipsEnabled(bool enabled) { m_TooltipsEnabled = enabled; }
    void setBookmarksEnabled(bool enabled) { m_BookmarksEnabled = enabled; }

    void setNewFttData(float *fftData, int size);
    void setNewFttData(float *fftData, float *wfData, int size);

    void setCenterFreq(quint64 f);
    void setFreqUnits(qint32 unit) { m_FreqUnits = unit; }
    void setDemodCenterFreq(quint64 f) { m_DemodCenterFreq = f; }

    void setFilterOffset(qint64 freq_hz)
    {
        m_DemodCenterFreq = m_CenterFreq + freq_hz;
        m_DrawOverlay = true;
        update();
    }
    qint64 getFilterOffset(void) { return m_DemodCenterFreq - m_CenterFreq; }
    int getFilterBw() { return m_DemodHiCutFreq - m_DemodLowCutFreq; }

    void setHiLowCutFrequencies(int LowCut, int HiCut)
    {
        m_DemodLowCutFreq = LowCut;
        m_DemodHiCutFreq = HiCut;
        m_DrawOverlay = true;
        update();
    }

    void getHiLowCutFrequencies(int *LowCut, int *HiCut)
    {
        *LowCut = m_DemodLowCutFreq;
        *HiCut = m_DemodHiCutFreq;
    }

    void setDemodRanges(int FLowCmin, int FLowCmax, int FHiCmin, int FHiCmax, bool symetric);

    void setSpanFreq(quint32 s)
    {
        if (s > 0 && s < INT_MAX) {
            m_Span = (qint32)s;
            setFftCenterFreq(m_FftCenter);
        }
        m_DrawOverlay = true;
        update();
    }

    void setHdivDelta(int delta) { m_HdivDelta = delta; }
    void setVdivDelta(int delta) { m_VdivDelta = delta; }
    void setFreqDigits(int digits) { m_FreqDigits = digits >= 0 ? digits : 0; }

    void setSampleRate(float rate)
    {
        if (rate > 0.0) {
            m_SampleFreq = rate;
            m_DrawOverlay = true;
            update();
        }
    }

    float getSampleRate(void) { return m_SampleFreq; }

    void setFftCenterFreq(qint64 f) {
        qint64 limit = ((qint64)m_SampleFreq + m_Span) / 2 - 1;
        m_FftCenter = qBound(-limit, f, limit);
    }

    int     getNearestPeak(QPoint pt);
    void    setWaterfallSpan(quint64 span_ms);
    quint64 getWfTimeRes(void);
    void    setFftRate(int rate_hz);
    void    clearWaterfall(void);
    bool    saveWaterfall(const QString & filename) const;
    void    setFreqStep(unsigned int newFreqStep);

    void setWaterfallPalette(int pal);

signals:
    void newCenterFreq(qint64 f);
    void newDemodFreq(qint64 freq, qint64 delta);
    void newLowCutFreq(int f);
    void newHighCutFreq(int f);
    void newFilterFreq(int low, int high);
    void pandapterRangeChanged(float min, float max);
    void newZoomLevel(float level);
    void wheelFreqChange(int direction); // +1 = up, -1 = down

public slots:
    void resetHorizontalZoom(void);
    void moveToCenterFreq(void);
    void moveToDemodFreq(void);
    void zoomOnXAxis(float level);

    void setFftPlotColor(const QColor color);
    void setFftFill(bool enabled);
    void setPeakHold(bool enabled);
    void setFftRange(float min, float max);
    void setPandapterRange(float min, float max);
    void setWaterfallRange(float min, float max);
    void setPeakDetection(bool enabled, float c);
    void updateOverlay();

    void setPercent2DScreen(int percent)
    {
        m_Percent2DScreen = percent;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // Drawing helpers
    void drawGrid(QPainter &painter, int w, int h);
    void drawSpectrum(QPainter &painter, int w, int h);
    void drawWaterfall(QPainter &painter, int w, int h);
    void drawFilterBox(QPainter &painter, int w, int h);
    void drawFreqLabels(QPainter &painter, int w, int spectrumH);
    void drawBandOverlay(QPainter &painter, int w, int specH);

    void makeFrequencyStrs();
    int  xFromFreq(qint64 freq);
    qint64 freqFromX(int x);
    void zoomStepX(float factor, int x);
    qint64 roundFreq(qint64 freq, int resolution);
    quint64 msecFromY(int y);
    void clampDemodParameters();
    bool isPointCloseTo(int x, int xr, int delta) { return ((x > (xr - delta)) && (x < (xr + delta))); }

    void getScreenIntegerFFTData(qint32 plotHeight, qint32 plotWidth,
                                 float maxdB, float mindB,
                                 qint64 startFreq, qint64 stopFreq,
                                 float *inBuf, qint32 *outBuf,
                                 int *xmin, int *xmax);

    void calcDivSize(qint64 low, qint64 high, int divswanted,
                     qint64 &adjlow, qint64 &step, int &divs);

    // Color table for waterfall
    QColor      m_ColorTbl[256];

    // FFT data (internal copies)
    std::mutex  m_fftMutex;
    std::vector<float> m_fftDataBuf;
    std::vector<float> m_wfDataBuf;
    float      *m_fftData{nullptr};
    float      *m_wfData{nullptr};
    int         m_fftDataSize{0};
    qint32      m_fftbuf[MAX_SCREENSIZE];

    // Waterfall as image
    QImage      m_waterfallImg;
    quint8      m_wfbuf[MAX_SCREENSIZE];

    // State
    bool        m_Running{false};
    bool        m_DrawOverlay{true};
    bool        m_PeakHoldActive{false};
    bool        m_PeakHoldValid{false};
    qint32      m_fftPeakHoldBuf[MAX_SCREENSIZE];

    // Frequency
    qint64      m_CenterFreq{0};
    qint64      m_FftCenter{0};
    qint64      m_DemodCenterFreq{0};
    qint64      m_StartFreqAdj{0};
    qint64      m_FreqPerDiv{0};
    qint64      m_Span{96000};
    float       m_SampleFreq{96000.0f};
    qint32      m_FreqUnits{1000000};
    int         m_FreqDigits{3};

    // Display ranges
    float       m_PandMindB{-160.0f};
    float       m_PandMaxdB{20.0f};
    float       m_WfMindB{-160.0f};
    float       m_WfMaxdB{20.0f};

    // UI state
    int         m_Percent2DScreen{50};
    int         m_ClickResolution{100};
    int         m_FilterClickResolution{100};
    int         m_CursorCaptureDelta{5};
    int         m_GrabPosition{0};
    bool        m_CenterLineEnabled{true};
    bool        m_FilterBoxEnabled{true};
    bool        m_TooltipsEnabled{false};
    bool        m_BookmarksEnabled{true};

    // Demod filter
    int         m_DemodHiCutFreq{0};
    int         m_DemodLowCutFreq{0};
    int         m_DemodFreqX{0};
    int         m_DemodHiCutFreqX{0};
    int         m_DemodLowCutFreqX{0};
    int         m_FLowCmin{-500000};
    int         m_FLowCmax{-1000};
    int         m_FHiCmin{1000};
    int         m_FHiCmax{500000};
    bool        m_symetric{true};

    // Grid
    int         m_HorDivs{12};
    int         m_VerDivs{6};
    int         m_HdivDelta{70};
    int         m_VdivDelta{30};
    int         m_YAxisWidth{0};
    int         m_XAxisYCenter{0};

    QString     m_HDivText[HORZ_DIVS_MAX + 1];

    // Capture
    enum eCapturetype { NOCAP, LEFT, CENTER, RIGHT, YAXIS, XAXIS, BOOKMARK };
    eCapturetype m_CursorCaptured{NOCAP};
    int         m_Xzero{0};
    int         m_Yzero{0};

    // Colors
    QColor      m_FftColor;
    QColor      m_FftFillCol;
    QColor      m_PeakHoldColor;
    bool        m_FftFill{true};

    // Peak detection
    float       m_PeakDetection{0.0f};
    QMap<int,int> m_Peaks;
    QList<QPair<QRect, qint64>> m_BookmarkTags;

    // Waterfall timing
    quint64     tlast_wf_ms{0};
    quint64     msec_per_wfline{0};
    quint64     wf_span{0};
    int         fft_rate{15};

    quint32     m_LastSampleRate{0};
    qreal       m_DPR{1.0};
    bool        m_InvertScrolling{false};
    bool        m_histIIRValid{false};
    qint32      m_CumWheelDelta{0};
    unsigned int freqStep{5};

    QFont       m_Font;
};

#endif // GLPLOTTER_H
