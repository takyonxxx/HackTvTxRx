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

    // Process incoming HackRF samples (int8_t IQ pairs)
    void processSamples(const int8_t* data, size_t len);
    
    // Process complex float samples
    void processSamples(const std::vector<std::complex<float>>& samples);

    // Get current frame as QImage
    QImage getCurrentFrame() const;
    
    // Configuration
    void setVideoGain(float gain) { m_videoGain = gain; }
    void setVideoOffset(float offset) { m_videoOffset = offset; }
    float getVideoGain() const { return m_videoGain; }
    float getVideoOffset() const { return m_videoOffset; }

signals:
    void frameReady(const QImage& frame);

private:
    // PAL-B/G Constants
    static constexpr int SAMP_RATE = 16000000;        // 16 MHz
    static constexpr int VIDEO_SAMP_RATE = 6000000;   // 6 MHz
    static constexpr int LINE_FREQ = 15625;           // Hz
    static constexpr int LINES_PER_FRAME = 625;
    static constexpr int VISIBLE_LINES = 576;         // Active video lines
    static constexpr int FIRST_VISIBLE_LINE = 49;     // Skip VBI
    static constexpr int SAMPLES_PER_LINE = VIDEO_SAMP_RATE / LINE_FREQ; // 384
    static constexpr int VIDEO_WIDTH = SAMPLES_PER_LINE;
    static constexpr int VIDEO_HEIGHT = VISIBLE_LINES;

    // FIR filter taps for low-pass filtering (5 MHz cutoff at 16 MHz)
    std::vector<float> m_videoFilterTaps;
    std::deque<std::complex<float>> m_videoFilterDelay;
    
    // FIR filter taps for luminance (3 MHz cutoff at 6 MHz)
    std::vector<float> m_lumaFilterTaps;
    std::deque<float> m_lumaFilterDelay;
    
    // DC blocker state
    float m_dcBlockerX1;
    float m_dcBlockerY1;
    
    // Resampler state
    int m_resampleCounter;
    int m_resampleDecim;
    
    // Video buffer
    std::vector<float> m_videoBuffer;  // Demodulated video at 6 MHz
    std::vector<uint8_t> m_frameBuffer; // Current frame buffer
    int m_currentLine;
    
    // Settings
    float m_videoGain;
    float m_videoOffset;
    
    // Helper functions
    void initFilters();
    std::vector<float> designLowPassFIR(float cutoff, float sampleRate, int numTaps);
    std::complex<float> applyVideoFilter(const std::complex<float>& sample);
    float applyLumaFilter(float sample);
    float dcBlock(float sample);
    void processVideoSample(float sample);
    void buildFrame();
    float clipValue(float value, float min, float max);
};

#endif // PALDECODER_H
