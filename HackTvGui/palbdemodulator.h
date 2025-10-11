#ifndef PALBDEMODULATOR_H
#define PALBDEMODULATOR_H

#include <QObject>
#include <QImage>
#include <QList>
#include <QMutex>
#include <vector>
#include <array>
#include <complex>
#include <QDebug>
#include <atomic>

class FrameBuffer
{
public:
    explicit FrameBuffer(double sampleRate = 16e6, double frameDuration = 0.04)
        : m_targetSize(0)
        , m_sampleRate(sampleRate)
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

        const qsizetype MAX_BUFFER_SIZE = 10000000;

        // Reserve space to avoid reallocation
        if (m_buffer.capacity() < MAX_BUFFER_SIZE) {
            m_buffer.reserve(MAX_BUFFER_SIZE);
        }

        // Efficiently append data
        qsizetype oldSize = m_buffer.size();
        m_buffer.resize(oldSize + newData.size());
        std::copy(newData.begin(), newData.end(), m_buffer.begin() + oldSize);

        // Trim from front if exceeded maximum
        if (m_buffer.size() > MAX_BUFFER_SIZE) {
            qsizetype excess = m_buffer.size() - MAX_BUFFER_SIZE;
            m_buffer.remove(0, excess);
        }

        m_bufferSize.store(m_buffer.size(), std::memory_order_release);
    }

    bool isFrameReady() const
    {
        return m_bufferSize.load(std::memory_order_acquire) >= m_targetSize.load(std::memory_order_acquire);
    }

    std::vector<std::complex<float>> getFrame()
    {
        if (m_buffer.size() < m_targetSize) {
            return std::vector<std::complex<float>>();
        }

        // Extract frame efficiently
        qsizetype target = m_targetSize.load(std::memory_order_acquire);
        std::vector<std::complex<float>> frame(target);
        std::copy(m_buffer.begin(), m_buffer.begin() + target, frame.begin());

        // Remove extracted samples from front
        m_buffer.remove(0, target);
        m_bufferSize.store(m_buffer.size(), std::memory_order_release);

        return frame;
    }

    void clear()
    {
        m_buffer.clear();
        m_bufferSize.store(0, std::memory_order_release);
    }

    qsizetype size() const
    {
        return m_bufferSize.load(std::memory_order_acquire);
    }

    qsizetype targetSize() const
    {
        return m_targetSize.load(std::memory_order_acquire);
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
        qsizetype target = m_targetSize.load(std::memory_order_acquire);
        if (target == 0) return 0.0f;
        qsizetype current = m_bufferSize.load(std::memory_order_acquire);
        return (static_cast<float>(current) / target) * 100.0f;
    }

private:
    void updateTargetSize()
    {
        qsizetype newTargetSize = static_cast<qsizetype>(m_sampleRate * m_frameDuration);
        m_targetSize.store(newTargetSize, std::memory_order_release);

        // Reserve space for multiple frames
        const qsizetype RESERVE_SIZE = newTargetSize * 3;
        if (m_buffer.capacity() < RESERVE_SIZE) {
            m_buffer.reserve(RESERVE_SIZE);
        }
    }

    QList<std::complex<float>> m_buffer;
    std::atomic<qsizetype> m_targetSize{0};
    std::atomic<qsizetype> m_bufferSize{0};
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
