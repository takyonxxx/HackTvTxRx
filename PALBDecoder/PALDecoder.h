#ifndef PALDECODER_H
#define PALDECODER_H

#include <QObject>
#include <QImage>
#include <vector>
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
    float getVideoGain() const { return m_videoGain; }
    float getVideoOffset() const { return m_videoOffset; }
    void setVideoInvert(bool invert) { m_videoInvert = invert; }
    bool getVideoInvert() const { return m_videoInvert; }

signals:
    void frameReady(const QImage& frame);

private:
    // PAL-B/G Constants
    static constexpr int SAMP_RATE = 16000000;
    static constexpr int VIDEO_SAMP_RATE = 6000000;
    static constexpr int LINE_FREQ = 15625;
    static constexpr int LINES_PER_FRAME = 625;
    static constexpr int VISIBLE_LINES = 576;
    static constexpr int FIRST_VISIBLE_LINE = 23;
    static constexpr int SAMPLES_PER_LINE = 384;
    static constexpr int VIDEO_WIDTH = 384;
    static constexpr int VIDEO_HEIGHT = 576;

    // PLL sync tracking
    int m_expectedSyncPosition;
    int m_samplesSinceSync;
    float m_syncConfidence;
    static constexpr int SYNC_SEARCH_WINDOW = 50;
    static constexpr int HSYNC_WIDTH = 30;

    // FIR filters
    std::vector<float> m_videoFilterTaps;
    std::deque<std::complex<float>> m_videoFilterDelay;
    std::vector<float> m_lumaFilterTaps;
    std::deque<float> m_lumaFilterDelay;

    // DC blocker
    float m_dcBlockerX1;
    float m_dcBlockerY1;

    // Resampler
    int m_resampleCounter;
    int m_resampleDecim;

    // Buffers
    std::vector<float> m_lineBuffer;
    std::vector<uint8_t> m_frameBuffer;
    int m_currentLine;
    int m_samplesInCurrentLine;

    // AGC
    float m_agcGain;
    float m_peakLevel;
    float m_minLevel;
    float m_meanLevel;

    // Sync detection buffer
    std::deque<float> m_sampleHistory;
    static constexpr int HISTORY_SIZE = 100;

    // Settings
    float m_videoGain;
    float m_videoOffset;
    bool m_videoInvert;

    // Stats
    uint64_t m_totalSamples;
    uint64_t m_frameCount;
    uint64_t m_linesProcessed;
    uint64_t m_syncDetected;

    // Helper functions
    void initFilters();
    std::vector<float> designLowPassFIR(float cutoff, float sampleRate, int numTaps);
    std::complex<float> applyVideoFilter(const std::complex<float>& sample);
    float applyLumaFilter(float sample);
    float dcBlock(float sample);
    float normalizeAndAGC(float sample);
    bool detectSyncPulse();
    void processVideoSample(float sample);
    void finalizeLine();
    void buildFrame();
    float clipValue(float value, float min, float max);
};

#endif // PALDECODER_H
