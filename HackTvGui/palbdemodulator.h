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
#include <deque>
#include <QDebug>
#include <cmath>

// ============================================================================
// THREAD-SAFE FRAME BUFFER
// ============================================================================
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

    // Get half frame for audio processing (with overlap)
    std::vector<std::complex<float>> getHalfFrame()
    {
        QMutexLocker lock(&m_mutex);

        qsizetype halfSize = m_targetSize / 2;

        if (m_buffer.size() < halfSize) {
            return std::vector<std::complex<float>>();
        }

        // Extract half frame
        std::vector<std::complex<float>> frame(halfSize);
        std::copy(m_buffer.begin(), m_buffer.begin() + halfSize, frame.begin());

        // Remove only 1/4 of the frame to maintain overlap for better audio continuity
        qsizetype removeSize = halfSize / 2;
        m_buffer.remove(0, removeSize);

        return frame;
    }

    // Get specific amount of samples for audio (non-destructive peek)
    std::vector<std::complex<float>> peekSamples(qsizetype numSamples) const
    {
        QMutexLocker lock(&m_mutex);

        if (m_buffer.size() < numSamples) {
            return std::vector<std::complex<float>>();
        }

        std::vector<std::complex<float>> samples(numSamples);
        std::copy(m_buffer.begin(), m_buffer.begin() + numSamples, samples.begin());

        return samples;
    }

    // Get and remove specific amount of samples
    std::vector<std::complex<float>> getSamples(qsizetype numSamples)
    {
        QMutexLocker lock(&m_mutex);

        if (m_buffer.size() < numSamples) {
            return std::vector<std::complex<float>>();
        }

        std::vector<std::complex<float>> samples(numSamples);
        std::copy(m_buffer.begin(), m_buffer.begin() + numSamples, samples.begin());

        // Remove extracted samples
        m_buffer.remove(0, numSamples);

        return samples;
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

    qsizetype halfTargetSize() const
    {
        QMutexLocker lock(&m_mutex);
        return m_targetSize / 2;
    }

    double sampleRate() const { return m_sampleRate; }
    double frameDuration() const { return m_frameDuration; }

    float fillPercentage() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_targetSize == 0) return 0.0f;
        return (static_cast<float>(m_buffer.size()) / m_targetSize) * 100.0f;
    }

    bool isHalfFrameReady() const
    {
        QMutexLocker lock(&m_mutex);
        return m_buffer.size() >= (m_targetSize / 2);
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
                 << "(" << m_frameDuration * 1000 << "ms)"
                 << "Half size:" << (m_targetSize / 2);
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
    virtual ~PALBDemodulator();

    struct DemodulatedFrame {
        QImage image;
        std::vector<float> audio;
        bool valid = false;
        int fieldNumber = 0;
    };
    // Add to PALBDemodulator
    enum DemodMode {
        DEMOD_FM,
        DEMOD_AM
    };

public:
    void setDemodMode(DemodMode mode) { demodMode = mode; }
    void setInvertVideo(bool invert) { invertVideo = invert; }

    // Main demodulation functions
    DemodulatedFrame demodulate(const std::vector<std::complex<float>>& samples);
    std::vector<float> demodulateAudioOnly(const std::vector<std::complex<float>>& samples);
    QImage demodulateVideoOnly(const std::vector<std::complex<float>>& samples);

    // Sample rate
    void setSampleRate(double rate);
    double getSampleRate() const { return sampleRate; }
    double getEffectiveSampleRate() const { return effectiveSampleRate; }

    // Carrier frequencies
    void setVideoCarrier(double freq) { videoCarrier = freq; }
    double getVideoCarrier() const { return videoCarrier; }
    void setAudioCarrier(double freq) { audioCarrier = freq; }
    double getAudioCarrier() const { return audioCarrier; }

    // Timing parameters
    void setHorizontalOffset(double offset) { horizontalOffset = offset; }
    double getHorizontalOffset() const { return horizontalOffset; }
    void setLineDuration(double duration) { lineDuration = duration; }
    double getLineDuration() const { return lineDuration; }

    // Image parameters
    void setPixelsPerLine(int pixels);
    int getPixelsPerLine() const { return pixelsPerLine; }
    void setVisibleLines(int lines) { visibleLines = lines; }
    int getVisibleLines() const { return visibleLines; }
    void setVBILines(int lines) { vbiLines = lines; }
    int getVBILines() const { return vbiLines; }

    // Processing parameters
    void setDecimationFactor(int factor);
    int getDecimationFactor() const { return decimationFactor; }
    void setAGCAttack(float rate) { agcAttackRate = rate; }
    void setAGCDecay(float rate) { agcDecayRate = rate; }
    float getAGCAttack() const { return agcAttackRate; }
    float getAGCDecay() const { return agcDecayRate; }
    void setVSyncThreshold(float threshold) { vSyncThreshold = threshold; }
    float getVSyncThreshold() const { return vSyncThreshold; }

    // FM deviation
    void setFMDeviation(double deviation) { fmDeviation = deviation; }
    double getFMDeviation() const { return fmDeviation; }

    // Interlacing control
    void setDeinterlace(bool enable) { enableDeinterlace = enable; }
    bool getDeinterlace() const { return enableDeinterlace; }

    // Video adjustment setters/getters
    void setVideoBrightness(float brightness) { m_brightness = brightness; }
    void setVideoContrast(float contrast) { m_contrast = contrast; }
    void setVideoGamma(float gamma) { m_gamma = gamma; }
    float getVideoBrightness() const { return m_brightness; }
    float getVideoContrast() const { return m_contrast; }
    float getVideoGamma() const { return m_gamma; }

    // Reset to defaults
    void resetToDefaults();

private:
    // ========================================================================
    // PAL-B/G STANDARD CONSTANTS
    // ========================================================================
    static constexpr double PAL_LINE_DURATION = 64e-6;         // 64 μs
    static constexpr double PAL_LINE_FREQUENCY = 15625.0;      // 15.625 kHz
    static constexpr double PAL_H_SYNC_DURATION = 4.7e-6;      // 4.7 μs
    static constexpr double PAL_BACK_PORCH = 5.7e-6;           // 5.7 μs
    static constexpr double PAL_FRONT_PORCH = 1.65e-6;         // 1.65 μs
    static constexpr double PAL_ACTIVE_VIDEO = 51.95e-6;       // 51.95 μs
    static constexpr double PAL_VSYNC_DURATION = 160e-6;       // 2.5 lines

    static constexpr int PAL_TOTAL_LINES = 625;
    static constexpr int PAL_VISIBLE_LINES = 576;
    static constexpr int PAL_VBI_LINES_PER_FIELD = 25;
    static constexpr double PAL_FIELD_RATE = 50.0;             // 50 Hz
    static constexpr double PAL_FRAME_RATE = 25.0;             // 25 fps

    static constexpr double AUDIO_CARRIER_OFFSET = 5.5e6;      // 5.5 MHz from video
    static constexpr double COLOR_SUBCARRIER = 4.43361875e6;   // 4.433619 MHz

    // Video signal levels (0-1 scale)
    static constexpr float SYNC_LEVEL = 0.0f;      // Sync tip level
    static constexpr float BLANKING_LEVEL = 0.3f;  // Blanking/black level
    static constexpr float BLACK_LEVEL = 0.3f;     // Black level
    static constexpr float WHITE_LEVEL = 1.0f;     // Peak white level

    DemodMode demodMode = DEMOD_FM;
    bool invertVideo = false;

    // ========================================================================
    // CONFIGURABLE PARAMETERS
    // ========================================================================
    double sampleRate;
    double effectiveSampleRate;     // After decimation
    double videoCarrier = 0.0;      // 0 for baseband
    double audioCarrier = AUDIO_CARRIER_OFFSET;
    double fmDeviation = 6.0e6;     // 6 MHz for UHF PAL

    // Timing
    double lineDuration = PAL_LINE_DURATION;
    double horizontalOffset = 0.148;  // ~9.5μs/64μs
    double lineFrequency = PAL_LINE_FREQUENCY;

    // Image dimensions
    int pixelsPerLine = 720;
    int visibleLines = 576;
    int vbiLines = PAL_VBI_LINES_PER_FIELD;

    // Calculated parameters
    double pointsPerLine = 0;
    double fractionalOffset = 0;
    int samplesPerLine = 0;

    // Processing
    int decimationFactor = 1;
    float agcAttackRate = 0.001f;
    float agcDecayRate = 0.0001f;
    float vSyncThreshold = 0.15f;

    // Video adjustments
    float m_brightness = 0.0f;  // -0.5 to +0.5
    float m_contrast = 1.0f;    // 0.5 to 2.0
    float m_gamma = 1.0f;       // 0.5 to 1.5

    // Interlacing
    bool enableDeinterlace = false;
    int currentField = 0;  // 0 = even, 1 = odd

    // AGC state
    float agcLevel = 1.0f;
    float peakLevel = 0.0f;

    // Sync detection state
    bool vSyncLocked = false;
    int vSyncCounter = 0;
    size_t lastVSyncPosition = 0;

    // Phase tracking for FM demod
    float lastPhase = 0.0f;
    std::deque<float> phaseHistory;

    // FIR filter coefficients
    std::vector<float> lowpassCoeffs;
    std::vector<float> videoFilterCoeffs;

    // Filter state buffers
    std::vector<std::complex<float>> complexFilterState;
    std::vector<float> realFilterState;

    // Line buffer for timing recovery
    std::vector<float> lineBuffer;
    size_t lineBufferIndex = 0;

    // ========================================================================
    // SIGNAL PROCESSING FUNCTIONS
    // ========================================================================

    // Initialization
    void initializeFilters();
    void calculateLineParameters();

    // Frequency operations
    std::vector<std::complex<float>> frequencyShift(
        const std::vector<std::complex<float>>& signal,
        double shiftFreq);

    // FM Demodulation methods
    std::vector<float> fmDemodulateAtan2(
        const std::vector<std::complex<float>>& signal);

    std::vector<float> fmDemodulateDifferential(
        const std::vector<std::complex<float>>& signal);

    // AM Demodulation
    std::vector<float> amDemodulate(
        const std::vector<std::complex<float>>& signal);

    // Filtering
    std::vector<float> designLowPassFIR(
        int numTaps,
        float cutoffFreq,
        float sampleRate);

    std::vector<std::complex<float>> complexLowPassFilter(
        const std::vector<std::complex<float>>& signal,
        float cutoffFreq);

    std::vector<float> lowPassFilter(
        const std::vector<float>& signal,
        float cutoffFreq);

    std::vector<float> applyFIRFilter(
        const std::vector<float>& signal,
        const std::vector<float>& coeffs);

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

    std::vector<float> normalizeSignal(
        const std::vector<float>& signal);

    // Synchronization
    bool detectVerticalSync(
        const std::vector<float>& signal,
        size_t& syncStart,
        int& fieldType);

    bool detectHorizontalSync(
        const std::vector<float>& signal,
        size_t startPos,
        size_t& syncPos);

    std::vector<float> removeVBI(
        const std::vector<float>& signal);

    // Timing recovery
    std::vector<float> timingRecovery(
        const std::vector<float>& signal);

    std::vector<float> interpolateLine(
        const std::vector<float>& signal,
        size_t startPos,
        int targetSamples);

    // Field processing
    std::vector<float> extractSingleField(
        const std::vector<float>& signal,
        bool oddField);

    std::vector<float> deinterlaceFields(
        const std::vector<float>& signal);

    // Image conversion
    QImage convertToImage(
        const std::vector<float>& videoSignal,
        float brightness = 0.0f,
        float contrast = 1.0f);

    QImage applyGammaCorrection(
        const QImage& image,
        float gamma);

    // Audio processing
    std::vector<float> demodulateAudioFM(
        const std::vector<std::complex<float>>& signal);

    // Helper functions
    float clamp(float value, float min, float max);
    uint8_t floatToUint8(float value);
    float unwrapPhase(float phase, float lastPhase);
};

#endif // PALBDEMODULATOR_H
