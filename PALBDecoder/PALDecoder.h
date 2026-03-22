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
    // ========== Sampling ==========
    static constexpr int SAMP_RATE = 16000000;
    static constexpr int DECIM = 3;
    static constexpr float DECIMATED_RATE = 16000000.0f / 3.0f;

    // ========== PAL-B/G Standard ==========
    static constexpr int NB_LINES = 625;
    static constexpr float FPS = 25.0f;
    static constexpr float LINE_DURATION = 64.0e-6f;  // 64 us

    // Timing in fractions of line duration (from ITU-R BT.1700)
    static constexpr float SYNC_PULSE_FRAC    = 4.7f / 64.0f;   // "d" sync pulse duration
    static constexpr float BLANKING_FRAC      = 12.0f / 64.0f;   // "a" line-blanking interval
    static constexpr float HSYNC_FRAC         = 10.5f / 64.0f;   // "b" sync + back porch
    static constexpr float HSYNC_CROP_FRAC    = 0.085f;           // crop from start (empirical)

    // VSync detection (from ITU-R BT.1700 Table 3)
    static constexpr float FIELD_DETECT_START = 2.35f / 64.0f;   // "p" equalizing pulse
    static constexpr float FIELD_DETECT_END   = 27.3f / 64.0f;   // "q" field sync pulse
    static constexpr float HALF_LINE          = 32.0f / 64.0f;

    // PAL-B/G specific
    static constexpr int VSYNC_LINES = 3;
    static constexpr int BLACK_LINES = 49;
    static constexpr int FIRST_VISIBLE_LINE = 23;

    // Output
    static constexpr int VIDEO_WIDTH = 720;
    static constexpr int VIDEO_HEIGHT = 576;

    // Color
    static constexpr float COLOR_CARRIER_FREQ = 4433618.75f;
    static constexpr float CHROMA_BANDWIDTH = 1.2e6f;

    mutable QMutex m_processMutex;

    // ========== NCO ==========
    double m_ncoPhase;
    double m_ncoPhaseIncrement;
    float m_videoCarrierOffsetHz;
    uint64_t m_tuneFrequency;
    void updateNCO();

    // ========== Sample Counter Line Timing (SDRangel approach) ==========
    int m_samplesPerLine;           // integer part
    float m_samplesPerLineFrac;     // fractional part
    int m_sampleOffset;             // current position within the line
    float m_sampleOffsetFrac;       // fractional position
    int m_sampleOffsetDetected;     // sample count since last detected hsync
    float m_hSyncShift;             // PLL correction to apply at end of line
    int m_hSyncErrorCount;          // consecutive large errors
    float m_prevSample;             // previous demodulated sample for zero-crossing

    // Computed timing values (in samples)
    int m_numberSamplesPerHTop;     // sync pulse width in samples
    int m_numberSamplesPerHSync;    // sync + back porch in samples
    int m_numberSamplesPerLineSignals; // blanking interval in samples
    int m_numberSamplesHSyncCrop;   // crop from line start

    // ========== VSync Detection ==========
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

    // ========== AGC (SDRangel 2-frame period) ==========
    float m_ampMin;
    float m_ampMax;
    float m_ampDelta;
    float m_effMin;    // tracking min within current period
    float m_effMax;    // tracking max within current period
    int m_amSampleIndex; // sample counter for AGC period

    // ========== Frame Buffer ==========
    std::vector<float> m_lineBuffer;
    std::vector<float> m_lineBufferU;
    std::vector<float> m_lineBufferV;
    std::vector<uint8_t> m_frameBuffer;

    // ========== User Controls ==========
    float m_videoGain;
    float m_videoOffset;
    bool m_videoInvert;
    float m_syncLevel;   // sync tip threshold (e.g. -0.2)
    bool m_colorMode;
    float m_chromaGain;
    bool m_hSyncEnabled;
    bool m_vSyncEnabled;

    // ========== Statistics ==========
    uint64_t m_totalSamples;
    uint64_t m_frameCount;
    uint64_t m_linesProcessed;
    uint64_t m_syncDetected;

    // ========== Color ==========
    bool m_vPhaseAlternate;
    std::vector<float> m_colorCarrierSin;
    std::vector<float> m_colorCarrierCos;
    int m_colorCarrierIndex;

    std::vector<float> m_prevLineU;
    std::vector<float> m_prevLineV;

    // ========== Methods ==========
    void initFilters();
    void applyStandard();
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
};

#endif
