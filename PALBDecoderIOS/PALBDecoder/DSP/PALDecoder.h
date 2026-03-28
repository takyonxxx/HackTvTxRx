#ifndef PALDECODER_H
#define PALDECODER_H

#include <vector>
#include <complex>
#include <cstdint>
#include <cmath>
#include <mutex>
#include <functional>
#include <cstring>
#include <algorithm>

// Fixed-size circular buffer for FIR delay lines - zero heap allocation
template<typename T, int MAX_TAPS>
struct FIRDelay {
    T buf[MAX_TAPS] = {};
    int pos = 0;
    int len = 0;

    void reset() { memset(buf, 0, sizeof(buf)); pos = 0; }
    void setLen(int n) { len = n; reset(); }

    inline void push(const T& val) {
        pos = (pos == 0) ? len - 1 : pos - 1;
        buf[pos] = val;
    }

    inline T apply(const float* taps) const {
        T out{};
        for (int i = 0; i < len; i++)
            out += buf[(pos + i) % len] * taps[i];
        return out;
    }
};

class PALDecoder
{
public:
    PALDecoder();
    ~PALDecoder();

    using FrameCallback = std::function<void(const uint8_t* rgbaData, int width, int height)>;
    using SyncStatsCallback = std::function<void(float syncRate, float peakLevel, float minLevel)>;

    void setFrameCallback(FrameCallback cb) { m_frameCallback = std::move(cb); }
    void setSyncStatsCallback(SyncStatsCallback cb) { m_syncStatsCallback = std::move(cb); }

    void processSamples(const int8_t* data, size_t len);
    void processSamples(const std::vector<std::complex<float>>& samples);

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

    static constexpr int VIDEO_WIDTH = 720;
    static constexpr int VIDEO_HEIGHT = 576;

private:
    static constexpr int NB_LINES = 625;
    static constexpr float FPS = 25.0f;
    static constexpr float SYNC_PULSE_FRAC    = 4.7f / 64.0f;
    static constexpr float BLANKING_FRAC      = 12.0f / 64.0f;
    static constexpr float HSYNC_FRAC         = 10.5f / 64.0f;
    static constexpr float HSYNC_CROP_FRAC    = 0.085f;
    static constexpr float FIELD_DETECT_START = 2.35f / 64.0f;
    static constexpr float FIELD_DETECT_END   = 27.3f / 64.0f;
    static constexpr float HALF_LINE          = 32.0f / 64.0f;
    static constexpr int VSYNC_LINES = 3;
    static constexpr int FIRST_VISIBLE_LINE = 23;
    static constexpr float COLOR_CARRIER_FREQ = 4433618.75f;
    static constexpr int MAX_FIR_TAPS = 72;  // max filter taps (65 + margin)

    std::mutex m_processMutex;
    FrameCallback m_frameCallback;
    SyncStatsCallback m_syncStatsCallback;

    int m_sampleRate;
    int m_decimFactor;
    float m_decimatedRate;
    float m_chromaBandwidth;

    double m_ncoPhase;
    double m_ncoPhaseIncrement;
    float m_videoCarrierOffsetHz;
    uint64_t m_tuneFrequency;
    void updateNCO();

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

    // FIR filters - fixed circular buffers (NO heap allocation per sample)
    std::vector<float> m_videoFilterTaps;
    FIRDelay<std::complex<float>, MAX_FIR_TAPS> m_videoFilterDelay;
    int m_videoFilterLen;

    std::vector<float> m_lumaFilterTaps;
    FIRDelay<float, 48> m_lumaFilterDelay;
    int m_lumaFilterLen;

    std::vector<float> m_chromaFilterTaps;
    FIRDelay<float, MAX_FIR_TAPS> m_chromaUFilterDelay;
    FIRDelay<float, MAX_FIR_TAPS> m_chromaVFilterDelay;
    int m_chromaFilterLen;

    float m_dcBlockerX1;
    float m_dcBlockerY1;
    int m_resampleCounter;
    float m_chromaUAccum;
    float m_chromaVAccum;

    float m_ampMin;
    float m_ampMax;
    float m_ampDelta;
    float m_effMin;
    float m_effMax;
    int m_amSampleIndex;

    std::vector<float> m_lineBuffer;
    std::vector<float> m_lineBufferU;
    std::vector<float> m_lineBufferV;
    std::vector<uint8_t> m_frameBuffer;

    float m_videoGain;
    float m_videoOffset;
    bool m_videoInvert;
    float m_syncLevel;
    bool m_colorMode;
    float m_chromaGain;
    bool m_hSyncEnabled;
    bool m_vSyncEnabled;

    uint64_t m_totalSamples;
    uint64_t m_frameCount;
    uint64_t m_linesProcessed;
    uint64_t m_syncDetected;
    uint64_t m_syncQualityWindow;
    uint64_t m_syncFoundInWindow;
    double   m_syncErrorAccum;
    float    m_lastSyncQuality;

    bool m_vPhaseAlternate;
    std::vector<float> m_colorCarrierSin;
    std::vector<float> m_colorCarrierCos;
    int m_colorCarrierIndex;
    std::vector<float> m_prevLineU;
    std::vector<float> m_prevLineV;

    void applyStandard();
    void initFilters();
    void rebuildColorLUT();
    std::vector<float> designLowPassFIR(float cutoff, float sampleRate, int numTaps);
    std::vector<float> designBandPassFIR(float centerFreq, float bandwidth, float sampleRate, int numTaps);

    inline std::complex<float> applyVideoFilter(const std::complex<float>& sample) {
        m_videoFilterDelay.push(sample);
        return m_videoFilterDelay.apply(m_videoFilterTaps.data());
    }
    inline float applyLumaFilter(float sample) {
        m_lumaFilterDelay.push(sample);
        return m_lumaFilterDelay.apply(m_lumaFilterTaps.data());
    }
    inline float applyChromaFilterU(float sample) {
        m_chromaUFilterDelay.push(sample);
        return m_chromaUFilterDelay.apply(m_chromaFilterTaps.data());
    }
    inline float applyChromaFilterV(float sample) {
        m_chromaVFilterDelay.push(sample);
        return m_chromaVFilterDelay.apply(m_chromaFilterTaps.data());
    }

    inline float dcBlock(float sample) {
        float out = sample - m_dcBlockerX1 + 0.995f * m_dcBlockerY1;
        m_dcBlockerX1 = sample;
        m_dcBlockerY1 = out;
        return out;
    }

    float normalizeAndAGC(float sample);
    void processSample(float sample);
    void processEndOfLine();
    void renderLine();
    void buildFrame();

    inline float clipValue(float value, float mn, float mx) {
        return value < mn ? mn : (value > mx ? mx : value);
    }
    void yuv2rgb(float y, float u, float v, uint8_t& r, uint8_t& g, uint8_t& b);
};

#endif
