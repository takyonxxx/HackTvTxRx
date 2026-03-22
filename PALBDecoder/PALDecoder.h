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

    void setVideoGain(float gain) { m_videoGain = gain; }
    void setVideoOffset(float offset) { m_videoOffset = offset; }
    void setVideoInvert(bool invert) { m_videoInvert = invert; }
    void setSyncThreshold(float threshold) { m_syncThreshold = threshold; }
    void setColorMode(bool color) { m_colorMode = color; }
    void setChromaGain(float gain) { m_chromaGain = gain; }

    // Set the center frequency the HackRF is tuned to (Hz)
    void setTuneFrequency(uint64_t freqHz);

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
    static constexpr int SAMP_RATE = 16000000;
    static constexpr int VIDEO_SAMP_RATE = 6000000;
    static constexpr int LINE_FREQ = 15625;
    static constexpr int LINES_PER_FRAME = 625;
    static constexpr int VISIBLE_LINES = 576;
    static constexpr int FIRST_VISIBLE_LINE = 23;
    static constexpr int SAMPLES_PER_LINE = 384;
    static constexpr int VIDEO_WIDTH = 576;
    static constexpr int VIDEO_HEIGHT = 384;
    
    static constexpr float COLOR_CARRIER_FREQ = 4433618.75f;
    static constexpr float CHROMA_BANDWIDTH = 1.5e6f;

    mutable QMutex m_processMutex;

    int m_expectedSyncPosition;
    int m_samplesSinceSync;
    float m_syncConfidence;
    static constexpr int SYNC_SEARCH_WINDOW = 50;
    static constexpr int HSYNC_WIDTH = 30;

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
    int m_resampleDecim;

    std::vector<float> m_lineBuffer;
    std::vector<float> m_lineBufferU;
    std::vector<float> m_lineBufferV;
    std::vector<uint8_t> m_frameBuffer;
    int m_currentLine;
    int m_samplesInCurrentLine;

    float m_agcGain;
    float m_peakLevel;
    float m_minLevel;
    float m_meanLevel;

    std::deque<float> m_sampleHistory;
    static constexpr int HISTORY_SIZE = 100;

    float m_videoGain;
    float m_videoOffset;
    bool m_videoInvert;
    float m_syncThreshold;
    bool m_colorMode;
    float m_chromaGain;

    uint64_t m_totalSamples;
    uint64_t m_frameCount;
    uint64_t m_linesProcessed;
    uint64_t m_syncDetected;

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

    // NCO for frequency shift (video carrier to DC)
    double m_ncoPhase;
    double m_ncoPhaseIncrement;
    float m_videoCarrierOffsetHz;
    uint64_t m_tuneFrequency;
    void updateNCO();

    void initFilters();
    std::vector<float> designLowPassFIR(float cutoff, float sampleRate, int numTaps);
    std::vector<float> designBandPassFIR(float centerFreq, float bandwidth, float sampleRate, int numTaps);
    std::complex<float> applyVideoFilter(const std::complex<float>& sample);
    float applyLumaFilter(float sample);
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
