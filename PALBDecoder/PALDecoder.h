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
    void setSyncThreshold(float threshold) { m_syncThreshold = threshold; }
    void setColorMode(bool color) { m_colorMode = color; }
    void setChromaGain(float gain) { m_chromaGain = gain; }

    float getVideoGain() const { return m_videoGain; }
    float getVideoOffset() const { return m_videoOffset; }
    bool getVideoInvert() const { return m_videoInvert; }
    float getSyncThreshold() const { return m_syncThreshold; }
    bool getColorMode() const { return m_colorMode; }
    float getChromaGain() const { return m_chromaGain; }

signals:
    void frameReady(const QImage& frame);
    void syncStatsUpdated(float syncRate, float peakLevel, float minLevel);

private:
    // ========== Sampling ==========
    static constexpr int SAMP_RATE = 16000000;
    static constexpr int DECIM = 3;
    // Actual decimated rate: 16000000/3 = 5333333.33 Hz
    static constexpr float DECIMATED_RATE = 16000000.0f / 3.0f;

    // ========== PAL-B/G Timing ==========
    static constexpr int LINE_FREQ = 15625;
    static constexpr int LINES_PER_FRAME = 625;
    static constexpr int FIELD_LINES = 312;  // 312.5 rounded - each field

    // At decimated rate: samples per line = 5333333/15625 = 341.33
    static constexpr int SAMPLES_PER_LINE = 341;

    // PAL-B blanking: ~12us total (front porch 1.65us + sync 4.7us + back porch 5.7us)
    // At 5.33 MHz: 12us * 5.33 = 64 samples blanking
    // Active video: ~52us * 5.33 = 277 samples
    static constexpr int BLANKING_SAMPLES = 64;
    static constexpr int ACTIVE_SAMPLES = 277;

    // ========== Interlace ==========
    // Field 1: lines 1-312 (odd field, displayed on rows 0,2,4,...)
    // Field 2: lines 313-625 (even field, displayed on rows 1,3,5,...)
    static constexpr int FIRST_VISIBLE_LINE_F1 = 23;   // First visible in field 1
    static constexpr int FIRST_VISIBLE_LINE_F2 = 336;   // First visible in field 2 (23+313)
    static constexpr int VISIBLE_LINES_PER_FIELD = 288; // ~288 visible lines per field

    // Output resolution
    static constexpr int VIDEO_WIDTH = 720;   // Standard PAL width
    static constexpr int VIDEO_HEIGHT = 576;  // 288 lines * 2 (interlaced)

    // ========== PAL Color ==========
    // Color subcarrier at DECIMATED rate (not VIDEO_SAMP_RATE which was wrong)
    static constexpr float COLOR_CARRIER_FREQ = 4433618.75f;
    static constexpr float CHROMA_BANDWIDTH = 1.2e6f;

    // ========== Sync ==========
    // Sync pulse: 4.7us * 5.33 MHz = ~25 samples
    static constexpr int HSYNC_WIDTH = 25;
    static constexpr int SYNC_SEARCH_WINDOW = 40;
    static constexpr int HISTORY_SIZE = 80;

    mutable QMutex m_processMutex;

    // ========== NCO ==========
    double m_ncoPhase;
    double m_ncoPhaseIncrement;
    float m_videoCarrierOffsetHz;
    uint64_t m_tuneFrequency;
    void updateNCO();

    // ========== Sync Tracking ==========
    int m_expectedSyncPosition;
    int m_samplesSinceSync;
    float m_syncConfidence;

    // ========== Interlace State ==========
    int m_currentField;     // 0 = field 1 (odd), 1 = field 2 (even)
    int m_fieldLineCount;   // Lines counted in current field
    bool m_inVSync;         // Currently in vertical sync region
    int m_vSyncCounter;     // Count consecutive short sync pulses

    // ========== Filters ==========
    std::vector<float> m_videoFilterTaps;
    std::deque<std::complex<float>> m_videoFilterDelay;
    std::vector<float> m_lumaFilterTaps;
    std::deque<float> m_lumaFilterDelay;
    std::vector<float> m_chromaFilterTaps;
    std::deque<float> m_chromaUFilterDelay;
    std::deque<float> m_chromaVFilterDelay;

    // Notch filter to remove color carrier from luma
    std::vector<float> m_lumaNotchTaps;
    std::deque<float> m_lumaNotchDelay;

    float m_dcBlockerX1;
    float m_dcBlockerY1;

    int m_resampleCounter;

    // ========== Line Buffers ==========
    std::vector<float> m_lineBuffer;
    std::vector<float> m_lineBufferU;
    std::vector<float> m_lineBufferV;
    std::vector<uint8_t> m_frameBuffer;
    int m_currentLine;
    int m_samplesInCurrentLine;

    // ========== AGC ==========
    float m_agcGain;
    float m_peakLevel;
    float m_minLevel;
    float m_meanLevel;

    std::deque<float> m_sampleHistory;

    // ========== User Controls ==========
    float m_videoGain;
    float m_videoOffset;
    bool m_videoInvert;
    float m_syncThreshold;
    bool m_colorMode;
    float m_chromaGain;

    // ========== Statistics ==========
    uint64_t m_totalSamples;
    uint64_t m_frameCount;
    uint64_t m_linesProcessed;
    uint64_t m_syncDetected;

    // ========== Color ==========
    float m_colorPhase;
    bool m_vPhaseAlternate;
    std::vector<float> m_colorCarrierSin;
    std::vector<float> m_colorCarrierCos;
    int m_colorCarrierIndex;
    float m_burstPhaseError;
    std::deque<float> m_burstHistory;
    static constexpr int BURST_SAMPLES = 25;

    // PAL delay line (comb filter)
    std::vector<float> m_prevLineU;
    std::vector<float> m_prevLineV;

    // ========== Methods ==========
    void initFilters();
    std::vector<float> designLowPassFIR(float cutoff, float sampleRate, int numTaps);
    std::vector<float> designBandPassFIR(float centerFreq, float bandwidth, float sampleRate, int numTaps);
    std::complex<float> applyVideoFilter(const std::complex<float>& sample);
    float applyLumaFilter(float sample);
    float applyLumaNotch(float sample);
    float applyChromaFilterU(float sample);
    float applyChromaFilterV(float sample);
    float dcBlock(float sample);
    float normalizeAndAGC(float sample);
    bool detectSyncPulse();
    void processVideoSample(float sample);
    void finalizeLine();
    void buildFrame();
    float clipValue(float value, float min, float max);
    void yuv2rgb(float y, float u, float v, uint8_t& r, uint8_t& g, uint8_t& b);
};

#endif
