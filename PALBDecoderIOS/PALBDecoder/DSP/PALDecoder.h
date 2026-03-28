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

template<typename T, int MAX_TAPS>
struct FIRDelay {
    T buf[MAX_TAPS] = {};
    int pos = 0, len = 0;
    void reset() { memset(buf, 0, sizeof(buf)); pos = 0; }
    void setLen(int n) { len = n; reset(); }
    inline void push(const T& val) { pos = (pos == 0) ? len - 1 : pos - 1; buf[pos] = val; }
    inline T apply(const float* taps) const {
        T out{}; for (int i = 0; i < len; i++) out += buf[(pos + i) % len] * taps[i]; return out;
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
    static constexpr float SYNC_PULSE_FRAC = 4.7f / 64.0f;
    static constexpr float BLANKING_FRAC = 12.0f / 64.0f;
    static constexpr float HSYNC_FRAC = 10.5f / 64.0f;
    static constexpr float FIELD_DETECT_START = 2.35f / 64.0f;
    static constexpr float FIELD_DETECT_END = 27.3f / 64.0f;
    static constexpr float HALF_LINE = 32.0f / 64.0f;
    static constexpr int VSYNC_LINES = 3;
    static constexpr int FIRST_VISIBLE_LINE = 23;
    static constexpr float COLOR_CARRIER_FREQ = 4433618.75f;
    static constexpr int MAX_FIR = 72;

    // NCO LUT
    static constexpr int NCO_LUT_BITS = 12;
    static constexpr int NCO_LUT_SIZE = 1 << NCO_LUT_BITS;  // 4096
    float m_ncoSin[NCO_LUT_SIZE];
    float m_ncoCos[NCO_LUT_SIZE];
    uint32_t m_ncoAccum = 0, m_ncoStep = 0;
    float m_videoCarrierOffsetHz = 0;
    uint64_t m_tuneFrequency = 479300000ULL;

    std::mutex m_processMutex;
    FrameCallback m_frameCallback;
    SyncStatsCallback m_syncStatsCallback;

    int m_sampleRate, m_decimFactor;
    float m_decimatedRate, m_chromaBandwidth;

    int m_samplesPerLine; float m_samplesPerLineFrac;
    int m_sampleOffset; float m_sampleOffsetFrac;
    int m_sampleOffsetDetected;
    float m_hSyncShift; int m_hSyncErrorCount; float m_prevSample;
    int m_numberSamplesPerHTop, m_numberSamplesPerHSync, m_numberSamplesPerLineSignals;
    int m_lineIndex, m_fieldIndex;
    int m_fieldDetectStartPos, m_fieldDetectEndPos;
    int m_vSyncDetectStartPos, m_vSyncDetectEndPos;
    int m_fieldDetectSampleCount, m_vSyncDetectSampleCount;
    int m_vSyncDetectThreshold, m_fieldDetectThreshold1, m_fieldDetectThreshold2;

    // Video IQ FIR - separate I and Q delay lines (avoid complex overhead)
    std::vector<float> m_videoFilterTaps;
    FIRDelay<float, MAX_FIR> m_vidFirI, m_vidFirQ;

    std::vector<float> m_lumaFilterTaps;
    FIRDelay<float, 48> m_lumaFir;

    std::vector<float> m_chromaFilterTaps;
    FIRDelay<float, MAX_FIR> m_chromaFirU, m_chromaFirV;

    float m_dcX1 = 0, m_dcY1 = 0;
    int m_resampleCounter = 0;
    float m_chromaUAccum = 0, m_chromaVAccum = 0;
    float m_ampMin, m_ampMax, m_ampDelta, m_effMin, m_effMax;
    int m_amSampleIndex = 0;

    std::vector<float> m_lineBuffer, m_lineBufferU, m_lineBufferV;
    std::vector<uint8_t> m_frameBuffer;

    float m_videoGain, m_videoOffset;
    bool m_videoInvert, m_colorMode;
    float m_syncLevel, m_chromaGain;
    bool m_hSyncEnabled, m_vSyncEnabled;

    uint64_t m_totalSamples, m_frameCount, m_linesProcessed, m_syncDetected;
    uint64_t m_syncQualityWindow, m_syncFoundInWindow;
    double m_syncErrorAccum; float m_lastSyncQuality;

    bool m_vPhaseAlternate;
    std::vector<float> m_colorCarrierSin, m_colorCarrierCos;
    int m_colorCarrierIndex;
    std::vector<float> m_prevLineU, m_prevLineV;

    void updateNCO();
    void applyStandard();
    void initFilters();
    void rebuildColorLUT();
    std::vector<float> designLowPassFIR(float cutoff, float sr, int n);
    std::vector<float> designBandPassFIR(float cf, float bw, float sr, int n);

    inline float dcBlock(float s) { float o = s - m_dcX1 + 0.995f * m_dcY1; m_dcX1 = s; m_dcY1 = o; return o; }
    float normalizeAndAGC(float sample);
    void processSample(float sample);
    void processEndOfLine();
    void renderLine();
    void buildFrame();
    inline float clip(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    void yuv2rgb(float y, float u, float v, uint8_t& r, uint8_t& g, uint8_t& b);
};

#endif
