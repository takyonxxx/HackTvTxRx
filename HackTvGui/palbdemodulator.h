#ifndef PALBDEMODULATOR_H
#define PALBDEMODULATOR_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <vector>
#include <array>
#include <complex>
#include <QDebug>

class FrameBuffer
{
public:
    explicit FrameBuffer(double sampleRate = 16e6, double frameDuration = 0.04)
        : m_sampleRate(sampleRate)
        , m_frameDuration(frameDuration)
    {
        updateTargetSize();
    }

    void setSampleRate(double sampleRate)
    {
        m_sampleRate = sampleRate;
        updateTargetSize();
    }

    void setFrameDuration(double duration)
    {
        m_frameDuration = duration;
        updateTargetSize();
    }

    void addBuffer(const std::vector<std::complex<float>>& newData)
    {
        if (newData.empty()) {
            return;
        }

        const size_t MAX_BUFFER_SIZE = 10000000;

        if (m_buffer.size() + newData.size() > MAX_BUFFER_SIZE) {
            m_buffer.clear();
            qWarning() << "Buffer cleared to prevent overflow";
        }

        try {
            m_buffer.insert(m_buffer.end(), newData.begin(), newData.end());
        }
        catch (const std::exception& e) {
            qCritical() << "Exception in addBuffer:" << e.what();
            m_buffer.clear();
        }
    }

    bool isFrameReady() const
    {
        return m_buffer.size() >= m_targetSize;
    }

    std::vector<std::complex<float>> getFrame()
    {
        if (m_buffer.size() < m_targetSize) {
            return std::vector<std::complex<float>>();
        }

        std::vector<std::complex<float>> frame;
        frame.reserve(m_targetSize);

        for (size_t i = 0; i < m_targetSize && i < m_buffer.size(); ++i) {
            frame.push_back(m_buffer[i]);
        }

        std::vector<std::complex<float>> remainingBuffer;
        if (m_buffer.size() > m_targetSize) {
            remainingBuffer.reserve(m_buffer.size() - m_targetSize);
            for (size_t i = m_targetSize; i < m_buffer.size(); ++i) {
                remainingBuffer.push_back(m_buffer[i]);
            }
        }

        m_buffer.swap(remainingBuffer);

        return frame;
    }

    void clear()
    {
        m_buffer.clear();
    }

    size_t size() const
    {
        return m_buffer.size();
    }

    size_t targetSize() const
    {
        return m_targetSize;
    }

    double sampleRate() const
    {
        return m_sampleRate;
    }

    double frameDuration() const
    {
        return m_frameDuration;
    }

    float fillPercentage() const
    {
        return (static_cast<float>(m_buffer.size()) / m_targetSize) * 100.0f;
    }

private:
    void updateTargetSize()
    {
        m_targetSize = static_cast<size_t>(m_sampleRate * m_frameDuration);
        m_buffer.reserve(m_targetSize * 2);
    }

    std::vector<std::complex<float>> m_buffer;
    size_t m_targetSize;
    double m_sampleRate;
    double m_frameDuration;
};

class PALBDemodulator : public QObject
{
    Q_OBJECT
public:
    explicit PALBDemodulator(double _sampleRate, QObject *parent = nullptr);

    struct DemodulatedFrame {
        QImage image;
        std::vector<float> audio;
    };

    DemodulatedFrame demodulate(const std::vector<std::complex<float>>& samples);

    void setSampleRate(double rate) { sampleRate = rate; }
    double getSampleRate() const { return sampleRate; }

    // AYARLANABILIR PARAMETRELER
    void setVideoCarrier(double freq) { videoCarrier = freq; }
    double getVideoCarrier() const { return videoCarrier; }

    void setHorizontalOffset(double offset) { horizontalOffset = offset; }
    double getHorizontalOffset() const { return horizontalOffset; }

    void setDecimationFactor(int factor) { decimationFactor = factor; }
    int getDecimationFactor() const { return decimationFactor; }

    void setAGCAttack(float rate) { agcAttackRate = rate; }
    void setAGCDecay(float rate) { agcDecayRate = rate; }
    float getAGCAttack() const { return agcAttackRate; }
    float getAGCDecay() const { return agcDecayRate; }

    void setVSyncThreshold(float threshold) { vSyncThreshold = threshold; }
    float getVSyncThreshold() const { return vSyncThreshold; }

    void setVBILines(int lines) { vbiLines = lines; }
    int getVBILines() const { return vbiLines; }

    void setLineDuration(double duration) { lineDuration = duration; }
    double getLineDuration() const { return lineDuration; }

    void setPixelsPerLine(int pixels) { pixelsPerLine = pixels; }
    int getPixelsPerLine() const { return pixelsPerLine; }

    void setVisibleLines(int lines) { visibleLines = lines; }
    int getVisibleLines() const { return visibleLines; }

private:
    // SABİT PARAMETRELER
    static constexpr double AUDIO_CARRIER = 5.74e6;
    static constexpr double COLOR_SUBCARRIER = 4.43361875e6;
    static constexpr int LINES_PER_FRAME = 625;
    static constexpr double FIELD_DURATION = 0.02;

    double sampleRate;

    // DEĞİŞKEN PARAMETRELER
    double videoCarrier = 5.5e6;
    double horizontalOffset = 0.15;
    int decimationFactor = 2;
    float agcAttackRate = 0.001f;
    float agcDecayRate = 0.0001f;
    float vSyncThreshold = 0.15f;
    int vbiLines = 25;
    double lineDuration = 64e-6;
    int pixelsPerLine = 720;
    int visibleLines = 576;

    std::array<float, 6> m_fltBufferI;
    std::array<float, 6> m_fltBufferQ;

    QMutex m_mutex;

    std::vector<float> extractSingleField(const std::vector<float>& signal, bool oddField);

    // Signal Processing
    std::vector<std::complex<float>> frequencyShift(
        const std::vector<std::complex<float>>& signal,
        double shiftFreq);

    std::vector<float> fmDemodulateYDiff(
        const std::vector<std::complex<float>>& signal);

    std::vector<float> amDemodulate(
        const std::vector<std::complex<float>>& signal);

    // Filters
    std::vector<float> lowPassFilter(
        const std::vector<float>& signal,
        float cutoffFreq);

    std::vector<std::complex<float>> complexLowPassFilter(
        const std::vector<std::complex<float>>& signal,
        float cutoffFreq);

    std::vector<float> designLowPassFIR(
        int numTaps,
        float cutoffFreq,
        float sampleRate);

    // Decimation
    std::vector<float> decimate(
        const std::vector<float>& signal,
        int factor);

    std::vector<std::complex<float>> decimateComplex(
        const std::vector<std::complex<float>>& signal,
        int factor);

    // Signal conditioning
    std::vector<float> removeDCOffset(
        const std::vector<float>& signal);

    std::vector<float> applyAGC(
        const std::vector<float>& signal);

    // Sync and timing
    bool detectVerticalSync(
        const std::vector<float>& signal,
        size_t& syncStart);

    std::vector<float> removeVBI(
        const std::vector<float>& signal);

    std::vector<std::complex<float>> removeVBIComplex(
        const std::vector<std::complex<float>>& signal);

    std::vector<float> timingRecovery(
        const std::vector<float>& signal);

    std::vector<std::complex<float>> timingRecoveryComplex(
        const std::vector<std::complex<float>>& signal);

    // Chroma extraction
    std::pair<std::vector<float>, std::vector<float>> extractChroma(
        const std::vector<std::complex<float>>& signal,
        size_t targetSize);

    // Image conversion
    QImage convertToImage(
        const std::vector<float>& videoSignal,
        float brightness,
        float contrast);

    QImage convertYUVtoRGB(
        const std::vector<float>& luma,
        const std::vector<float>& chromaU,
        const std::vector<float>& chromaV);
};

#endif // PALBDEMODULATOR_H
