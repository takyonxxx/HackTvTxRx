#ifndef PALBDEMODULATOR_H
#define PALBDEMODULATOR_H

#include <QObject>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <vector>
#include <array>
#include <complex>
#include <QDebug>

// ============================================================================
// THREAD-SAFE FRAME BUFFER
// ============================================================================
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
        QMutexLocker lock(&m_mutex);
        m_sampleRate = sampleRate;
        updateTargetSize();
    }

    void setFrameDuration(double duration)
    {
        QMutexLocker lock(&m_mutex);
        m_frameDuration = duration;
        updateTargetSize();
    }

    void addBuffer(const std::vector<std::complex<float>>& newData)
    {
        if (newData.empty()) return;

        QMutexLocker lock(&m_mutex);

        const qsizetype MAX_BUFFER_SIZE = 10000000;

        // Reserve capacity to prevent reallocation
        if (m_buffer.capacity() < MAX_BUFFER_SIZE) {
            m_buffer.reserve(MAX_BUFFER_SIZE);
        }

        // Efficient batch append
        qsizetype oldSize = m_buffer.size();
        m_buffer.resize(oldSize + newData.size());
        std::copy(newData.begin(), newData.end(), m_buffer.begin() + oldSize);

        // Trim old data if exceeded
        if (m_buffer.size() > MAX_BUFFER_SIZE) {
            qsizetype excess = m_buffer.size() - MAX_BUFFER_SIZE;
            m_buffer.remove(0, excess);
        }
    }

    bool isFrameReady() const
    {
        QMutexLocker lock(&m_mutex);
        return m_buffer.size() >= m_targetSize;
    }

    std::vector<std::complex<float>> getFrame()
    {
        QMutexLocker lock(&m_mutex);

        if (m_buffer.size() < m_targetSize) {
            return std::vector<std::complex<float>>();
        }

        // Extract frame
        std::vector<std::complex<float>> frame(m_targetSize);
        std::copy(m_buffer.begin(), m_buffer.begin() + m_targetSize, frame.begin());

        // Remove extracted samples
        m_buffer.remove(0, m_targetSize);

        return frame;
    }

    std::vector<std::complex<float>> getHalfFrame()
    {
        QMutexLocker lock(&m_mutex);

        qsizetype halfSize = m_targetSize / 2;

        if (m_buffer.size() < halfSize) {
            return std::vector<std::complex<float>>();
        }

        // Yarım frame çıkar
        std::vector<std::complex<float>> frame(halfSize);
        std::copy(m_buffer.begin(), m_buffer.begin() + halfSize, frame.begin());

        // Sadece yarısını sil (overlap için)
        m_buffer.remove(0, halfSize / 2);

        return frame;
    }

    void clear()
    {
        QMutexLocker lock(&m_mutex);
        m_buffer.clear();
    }

    qsizetype size() const
    {
        QMutexLocker lock(&m_mutex);
        return m_buffer.size();
    }

    qsizetype targetSize() const
    {
        QMutexLocker lock(&m_mutex);
        return m_targetSize;
    }

    double sampleRate() const { return m_sampleRate; }
    double frameDuration() const { return m_frameDuration; }

    float fillPercentage() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_targetSize == 0) return 0.0f;
        return (static_cast<float>(m_buffer.size()) / m_targetSize) * 100.0f;
    }

private:
    void updateTargetSize()
    {
        m_targetSize = static_cast<qsizetype>(m_sampleRate * m_frameDuration);

        const qsizetype RESERVE_SIZE = m_targetSize * 3;
        if (m_buffer.capacity() < RESERVE_SIZE) {
            m_buffer.reserve(RESERVE_SIZE);
        }

        qDebug() << "FrameBuffer target size:" << m_targetSize
                 << "(" << m_frameDuration * 1000 << "ms)";
    }

    QList<std::complex<float>> m_buffer;
    qsizetype m_targetSize;
    double m_sampleRate;
    double m_frameDuration;
    mutable QMutex m_mutex;
};

// ============================================================================
// PAL-B DEMODULATOR
// ============================================================================
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

    // Sample rate
    void setSampleRate(double rate) { sampleRate = rate; }
    double getSampleRate() const { return sampleRate; }

    // Carrier frequencies
    void setVideoCarrier(double freq) { videoCarrier = freq; }
    double getVideoCarrier() const { return videoCarrier; }

    // Timing parameters
    void setHorizontalOffset(double offset) { horizontalOffset = offset; }
    double getHorizontalOffset() const { return horizontalOffset; }

    void setLineDuration(double duration) { lineDuration = duration; }
    double getLineDuration() const { return lineDuration; }

    // Image parameters
    void setPixelsPerLine(int pixels) { pixelsPerLine = pixels; }
    int getPixelsPerLine() const { return pixelsPerLine; }

    void setVisibleLines(int lines) { visibleLines = lines; }
    int getVisibleLines() const { return visibleLines; }

    void setVBILines(int lines) { vbiLines = lines; }
    int getVBILines() const { return vbiLines; }

    // Processing parameters
    void setDecimationFactor(int factor) { decimationFactor = factor; }
    int getDecimationFactor() const { return decimationFactor; }

    void setAGCAttack(float rate) { agcAttackRate = rate; }
    void setAGCDecay(float rate) { agcDecayRate = rate; }
    float getAGCAttack() const { return agcAttackRate; }
    float getAGCDecay() const { return agcDecayRate; }

    void setVSyncThreshold(float threshold) { vSyncThreshold = threshold; }
    float getVSyncThreshold() const { return vSyncThreshold; }

    // Interlacing control
    void setDeinterlace(bool enable) { enableDeinterlace = enable; }
    bool getDeinterlace() const { return enableDeinterlace; }

    std::vector<float> demodulateAudioOnly(const std::vector<std::complex<float>>& samples);
    QImage demodulateVideoOnly(const std::vector<std::complex<float>>& samples);

private:
         // ========================================================================
    // PAL-B/G STANDARD CONSTANTS
    // ========================================================================
    static constexpr double PAL_LINE_DURATION = 64e-6;         // 64 μs
    static constexpr double PAL_H_SYNC_DURATION = 4.7e-6;      // 4.7 μs
    static constexpr double PAL_BACK_PORCH = 5.7e-6;           // 5.7 μs
    static constexpr double PAL_FRONT_PORCH = 1.65e-6;         // 1.65 μs
    static constexpr double PAL_ACTIVE_VIDEO = 51.95e-6;       // 51.95 μs

    static constexpr int PAL_TOTAL_LINES = 625;
    static constexpr int PAL_VISIBLE_LINES = 576;
    static constexpr int PAL_VBI_LINES_PER_FIELD = 25;

    static constexpr double PAL_FIELD_RATE = 50.0;             // 50 Hz
    static constexpr double PAL_FRAME_RATE = 25.0;             // 25 fps

    static constexpr double AUDIO_CARRIER = 5.74e6;            // 5.74 MHz
    static constexpr double COLOR_SUBCARRIER = 4.43361875e6;   // 4.433619 MHz

    // ========================================================================
    // CONFIGURABLE PARAMETERS
    // ========================================================================
    double sampleRate;
    double videoCarrier = 5.5e6;

    // Timing
    double lineDuration = PAL_LINE_DURATION;
    double horizontalOffset = (PAL_H_SYNC_DURATION + PAL_BACK_PORCH) / PAL_LINE_DURATION;

    // Image dimensions
    int pixelsPerLine = 702;        // Standard PAL: 702 or 720
    int visibleLines = 576;         // Single field: 288 lines (576/2)
    int vbiLines = PAL_VBI_LINES_PER_FIELD;

    // Processing
    int decimationFactor = 2;
    float agcAttackRate = 0.001f;
    float agcDecayRate = 0.0001f;
    float vSyncThreshold = 0.15f;

    // Interlacing
    bool enableDeinterlace = false;

    // Filter state
    std::array<float, 6> m_fltBufferI;
    std::array<float, 6> m_fltBufferQ;

    // ========================================================================
    // SIGNAL PROCESSING FUNCTIONS (MOVE-ENABLED)
    // ========================================================================

    // Frequency operations
    std::vector<std::complex<float>> frequencyShift(
        std::vector<std::complex<float>> signal,
        double shiftFreq);

    // Demodulation
    std::vector<float> amDemodulate(
        const std::vector<std::complex<float>>& signal);

    std::vector<float> fmDemodulateYDiff(
        const std::vector<std::complex<float>>& signal);

    // Filtering
    std::vector<float> designLowPassFIR(
        int numTaps,
        float cutoffFreq,
        float sampleRate);

    std::vector<std::complex<float>> complexLowPassFilter(
        std::vector<std::complex<float>> signal,
        float cutoffFreq);

    std::vector<float> lowPassFilter(
        std::vector<float> signal,
        float cutoffFreq);

    // Decimation
    std::vector<float> decimate(
        std::vector<float> signal,
        int factor);

    std::vector<std::complex<float>> decimateComplex(
        std::vector<std::complex<float>> signal,
        int factor);

    // Signal conditioning
    std::vector<float> removeDCOffset(
        std::vector<float> signal);

    std::vector<float> applyAGC(
        std::vector<float> signal);

    // Synchronization
    bool detectVerticalSync(
        const std::vector<float>& signal,
        size_t& syncStart);

    std::vector<float> removeVBI(
        std::vector<float> signal);

    std::vector<std::complex<float>> removeVBIComplex(
        std::vector<std::complex<float>> signal);

    // Timing recovery
    std::vector<float> timingRecovery(
        std::vector<float> signal);

    std::vector<std::complex<float>> timingRecoveryComplex(
        std::vector<std::complex<float>> signal);

    // Field processing
    std::vector<float> extractSingleField(
        std::vector<float> signal,
        bool oddField);

    std::vector<float> deinterlaceFields(
        std::vector<float> signal);

    // Chroma
    std::pair<std::vector<float>, std::vector<float>> extractChroma(
        const std::vector<std::complex<float>>& signal,
        size_t targetSize);

    // Image conversion
    QImage convertToImage(
        const std::vector<float>& videoSignal,
        float brightness = 0.0f,
        float contrast = 1.0f);

    QImage convertYUVtoRGB(
        const std::vector<float>& luma,
        const std::vector<float>& chromaU,
        const std::vector<float>& chromaV);

    std::vector<float> fmDemodulateAudio(
        const std::vector<std::complex<float>>& signal,
        double quadratureRate);

    std::vector<float> applyAudioLowPassFilter(
        std::vector<float> signal);

    float softClip(float x);
};

#endif // PALBDEMODULATOR_H
