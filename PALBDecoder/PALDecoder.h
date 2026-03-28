#ifndef PALDECODER_H
#define PALDECODER_H

#include <QObject>
#include <QImage>
#include <vector>
#include <QMutex>
#include <complex>
#include <deque>
#include <cstdint>
#include <cmath>

class PALDecoder : public QObject
{
    Q_OBJECT
public:
    explicit PALDecoder(QObject *parent = nullptr);
    ~PALDecoder();

    void processSamples(const int8_t* data, size_t len);
    void processSamples(const std::vector<std::complex<float>>& samples);
    QImage getCurrentFrame() const;

    void setTuneFrequency(uint64_t freqHz);
    void setSampleRate(int sampleRate);

    void setVideoGain(float gain) { m_videoGain = gain; }
    void setVideoOffset(float offset) { m_videoOffset = offset; }
    void setVideoInvert(bool invert) { m_videoInvert = invert; }
    void setSyncThreshold(float threshold) { m_syncLevel = threshold; }
    void setColorMode(bool color) { m_colorMode = color; }
    void setChromaGain(float gain) { m_chromaGain = gain; }

    float getVideoGain() const { return m_videoGain; }
    float getVideoOffset() const { return m_videoOffset; }
    bool getVideoInvert() const { return m_videoInvert; }
    float getSyncThreshold() const { return m_syncLevel; }
    bool getColorMode() const { return m_colorMode; }
    float getChromaGain() const { return m_chromaGain; }

signals:
    void frameReady(const QImage& frame);
    void syncStatsUpdated(float syncRate, float peakLevel, float minLevel);

private:
    // ========== PAL-B/G Standard (fixed) ==========
    static constexpr int NB_LINES = 625;
    static constexpr float FPS = 25.0f;
    static constexpr float LINE_DURATION_US = 64.0f;

    // Timing fractions (ITU-R BT.1700)
    static constexpr float SYNC_PULSE_FRAC    = 4.7f / 64.0f;
    static constexpr float BLANKING_FRAC      = 12.0f / 64.0f;
    static constexpr float HSYNC_FRAC         = 10.5f / 64.0f;
    static constexpr float HSYNC_CROP_FRAC    = 0.085f;
    static constexpr float FIELD_DETECT_START = 2.35f / 64.0f;
    static constexpr float FIELD_DETECT_END   = 27.3f / 64.0f;
    static constexpr float HALF_LINE          = 32.0f / 64.0f;

    static constexpr int VSYNC_LINES = 3;
    static constexpr int FIRST_VISIBLE_LINE = 23;

    static constexpr int VIDEO_WIDTH = 720;
    static constexpr int VIDEO_HEIGHT = 576;

    static constexpr float COLOR_CARRIER_FREQ = 4433618.75f;

    mutable QMutex m_processMutex;

    // ========== Dynamic Sample Rate ==========
    int m_sampleRate;           // input sample rate (8-20 MHz)
    int m_decimFactor;          // decimation factor (1, 2, or 3)
    float m_decimatedRate;      // m_sampleRate / m_decimFactor
    float m_chromaBandwidth;    // adjusted per rate

    // ========== NCO ==========
    double m_ncoPhase;
    double m_ncoPhaseIncrement;
    float m_videoCarrierOffsetHz;
    uint64_t m_tuneFrequency;
    void updateNCO();

    // ========== Sample Counter Line Timing (at m_sampleRate) ==========
    int m_samplesPerLine;
    float m_samplesPerLineFrac;
    int m_sampleOffset;
    float m_sampleOffsetFrac;
    int m_sampleOffsetDetected;
    float m_hSyncShift;
    int m_hSyncErrorCount;
    float m_prevSample;

    int m_numberSamplesPerHTop;
    int m_numberSamplesPerHSync;
    int m_numberSamplesPerLineSignals;
    int m_numberSamplesHSyncCrop;

    // ========== VSync ==========
    int m_lineIndex;
    int m_fieldIndex;
    int m_fieldDetectStartPos;
    int m_fieldDetectEndPos;
    int m_vSyncDetectStartPos;
    int m_vSyncDetectEndPos;
    int m_fieldDetectSampleCount;
    int m_vSyncDetectSampleCount;
    int m_vSyncDetectThreshold;
    int m_fieldDetectThreshold1;
    int m_fieldDetectThreshold2;

    // ========== Filters ==========
    std::vector<float> m_videoFilterTaps;
    std::deque<std::complex<float>> m_videoFilterDelay;
    std::vector<float> m_lumaFilterTaps;
    std::deque<float> m_lumaFilterDelay;
    std::vector<float> m_chromaFilterTaps;
    std::deque<float> m_chromaUFilterDelay;
    std::deque<float> m_chromaVFilterDelay;

    float m_dcBlockerX1;
    float m_dcBlockerY1;
    int m_resampleCounter;

    // Audio carrier notch filter (IIR biquad) - removes 5.5 MHz beat after AM demod
    float m_notchB0, m_notchB1, m_notchB2, m_notchA1, m_notchA2;
    float m_notchX1, m_notchX2, m_notchY1, m_notchY2;

    // Chroma subcarrier notch (4.43 MHz) - removes subcarrier from luma to prevent color stripes
    // Cascaded 2-stage biquad for deeper notch (~40 dB instead of ~20 dB)
    float m_chromaNotchB0, m_chromaNotchB1, m_chromaNotchB2, m_chromaNotchA1, m_chromaNotchA2;
    float m_chromaNotchX1, m_chromaNotchX2, m_chromaNotchY1, m_chromaNotchY2;
    // Second stage (same coefficients, independent state)
    float m_chromaNotch2X1, m_chromaNotch2X2, m_chromaNotch2Y1, m_chromaNotch2Y2;

    // Chroma accumulators (full-rate chroma demod, averaged over decimation period)
    float m_chromaUAccum;
    float m_chromaVAccum;

    // ========== AGC ==========
    float m_ampMin;
    float m_ampMax;
    float m_ampDelta;
    float m_effMin;
    float m_effMax;
    int m_amSampleIndex;

    // ========== Frame Buffer ==========
    std::vector<float> m_lineBuffer;
    std::vector<float> m_lineBufferU;
    std::vector<float> m_lineBufferV;
    std::vector<uint8_t> m_frameBuffer;

    // ========== User Controls ==========
    float m_videoGain;
    float m_videoOffset;
    bool m_videoInvert;
    float m_syncLevel;
    bool m_colorMode;
    float m_chromaGain;
    bool m_hSyncEnabled;
    bool m_vSyncEnabled;

    // ========== Statistics ==========
    uint64_t m_totalSamples;
    uint64_t m_frameCount;
    uint64_t m_linesProcessed;
    uint64_t m_syncDetected;

    // Flywheel-based sync quality:
    // Accumulate abs(hSyncShift) for lines where sync WAS found,
    // and count lines where sync was NOT found (free-running).
    // syncQuality = weighted metric: low error + high detection = good.
    uint64_t m_syncQualityWindow;      // lines in current measurement window
    uint64_t m_syncFoundInWindow;      // lines where zero-crossing was detected
    double   m_syncErrorAccum;         // sum of |hSyncShift| in window
    float    m_lastSyncQuality;        // 0..100 computed at each reporting interval

    // ========== Color ==========
    bool m_vPhaseAlternate;
    std::vector<float> m_colorCarrierSin;
    std::vector<float> m_colorCarrierCos;
    int m_colorCarrierIndex;
    std::vector<float> m_prevLineU;
    std::vector<float> m_prevLineV;

    // ========== Colour Burst PLL ==========
    // Back porch burst window (samples at full rate)
    int m_burstStartSample;       // start of burst window (~5.6 us from line start)
    int m_burstEndSample;         // end of burst window (~7.85 us)

    // Burst correlation accumulators (per line)
    float m_burstCorrI;           // sum(sample * cos(2pi*fsc*t))
    float m_burstCorrQ;           // sum(sample * sin(2pi*fsc*t))
    float m_burstDCAccum;         // sum(sample) for DC removal
    float m_burstCosAccum;        // sum(cos(2pi*fsc*t)) for DC removal
    float m_burstSinAccum;        // sum(sin(2pi*fsc*t)) for DC removal
    int   m_burstSampleCount;     // samples accumulated in burst window

    // Extracted burst phase & amplitude
    float m_burstAmplitude;       // measured burst amplitude (for chroma AGC)
    bool  m_burstValid;           // true if burst was detected this line

    // Phase-locked reference (derived from burst)
    float m_chromaRefPhase;       // reference phase for chroma demod this line
    float m_burstPhaseSmoothed;   // low-pass filtered burst phase (reduces jitter)

    // Cached sin/cos of reference phase (computed once per line after burst extraction)
    float m_chromaCosRef;         // cos(m_chromaRefPhase)
    float m_chromaSinRef;         // sin(m_chromaRefPhase)

    // Burst amplitude AGC
    float m_burstAmpSmoothed;     // smoothed burst amplitude for chroma scaling

    // ========== Methods ==========
    void applyStandard();
    void initFilters();
    void initNotchFilter();
    void initBurstPLL();
    void rebuildColorLUT();
    std::vector<float> designLowPassFIR(float cutoff, float sampleRate, int numTaps);
    std::vector<float> designBandPassFIR(float centerFreq, float bandwidth, float sampleRate, int numTaps);
    std::complex<float> applyVideoFilter(const std::complex<float>& sample);
    float applyLumaFilter(float sample);
    float applyChromaFilterU(float sample);
    float applyChromaFilterV(float sample);
    float dcBlock(float sample);
    float normalizeAndAGC(float sample);
    void processSample(float sample);
    void processEndOfLine();
    void renderLine();
    void buildFrame();
    float clipValue(float value, float min, float max);
    void yuv2rgb(float y, float u, float v, uint8_t& r, uint8_t& g, uint8_t& b);
    void accumulateBurst(float sample);
    void extractBurstPhase();
    float chromaDemodU(float sample);
    float chromaDemodV(float sample);
};

#endif
