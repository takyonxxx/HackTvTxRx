#ifndef PALBDEMODULATOR_H
#define PALBDEMODULATOR_H

#include <QObject>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QRecursiveMutex>
#include <QMutexLocker>
#include <vector>
#include <array>
#include <complex>
#include <deque>
#include <QDebug>
#include <cmath>

// Forward declarations for DATV-style integration
class TVScreen;

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

        if (m_buffer.capacity() < MAX_BUFFER_SIZE) {
            m_buffer.reserve(MAX_BUFFER_SIZE);
        }

        qsizetype oldSize = m_buffer.size();
        m_buffer.resize(oldSize + newData.size());
        std::copy(newData.begin(), newData.end(), m_buffer.begin() + oldSize);

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

        std::vector<std::complex<float>> frame(m_targetSize);
        std::copy(m_buffer.begin(), m_buffer.begin() + m_targetSize, frame.begin());
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

        std::vector<std::complex<float>> frame(halfSize);
        std::copy(m_buffer.begin(), m_buffer.begin() + halfSize, frame.begin());

        qsizetype removeSize = halfSize / 2;
        m_buffer.remove(0, removeSize);

        return frame;
    }

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

    std::vector<std::complex<float>> getSamples(qsizetype numSamples)
    {
        QMutexLocker lock(&m_mutex);

        if (m_buffer.size() < numSamples) {
            return std::vector<std::complex<float>>();
        }

        std::vector<std::complex<float>> samples(numSamples);
        std::copy(m_buffer.begin(), m_buffer.begin() + numSamples, samples.begin());
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

    enum DemodMode {
        DEMOD_FM,
        DEMOD_AM
    };

    void setTVScreen(TVScreen *tvScreen) {
        QMutexLocker lock(&m_mutex);
        m_tvScreen = tvScreen;
    }

    TVScreen* getTVScreen() const { return m_tvScreen; }

    // Rendering control
    void renderToTVScreen();
    bool isTVScreenAvailable() const { return m_tvScreen != nullptr; }

    // Demodulation mode control
    void setDemodMode(DemodMode mode) {
        QMutexLocker lock(&m_mutex);
        demodMode = mode;
    }
    DemodMode getDemodMode() const { return demodMode; }
    void setInvertVideo(bool invert) {
        QMutexLocker lock(&m_mutex);
        invertVideo = invert;
    }
    bool getInvertVideo() const { return invertVideo; }

    // Main demodulation functions
    DemodulatedFrame demodulate(const std::vector<std::complex<float>>& samples);
    std::vector<float> demodulateAudioOnly(const std::vector<std::complex<float>>& samples);
    QImage demodulateVideoOnly(const std::vector<std::complex<float>>& samples);

    // Sample rate
    void setSampleRate(double rate);
    double getSampleRate() const { return sampleRate; }
    double getEffectiveSampleRate() const { return effectiveSampleRate; }

    // Carrier frequencies
    void setVideoCarrier(double freq) {
        QMutexLocker lock(&m_mutex);
        videoCarrier = freq;
    }
    double getVideoCarrier() const { return videoCarrier; }
    void setAudioCarrier(double freq) {
        QMutexLocker lock(&m_mutex);
        audioCarrier = freq;
    }
    double getAudioCarrier() const { return audioCarrier; }

    // Timing parameters
    void setHorizontalOffset(double offset) {
        QMutexLocker lock(&m_mutex);
        horizontalOffset = offset;
    }
    double getHorizontalOffset() const { return horizontalOffset; }
    void setLineDuration(double duration) {
        QMutexLocker lock(&m_mutex);
        lineDuration = duration;
        calculateLineParameters();
    }
    double getLineDuration() const { return lineDuration; }

    // Image parameters
    void setPixelsPerLine(int pixels);
    int getPixelsPerLine() const { return pixelsPerLine; }
    void setVisibleLines(int lines) {
        QMutexLocker lock(&m_mutex);
        visibleLines = lines;
    }
    int getVisibleLines() const { return visibleLines; }
    void setVBILines(int lines) {
        QMutexLocker lock(&m_mutex);
        vbiLines = lines;
    }
    int getVBILines() const { return vbiLines; }

    // Processing parameters
    void setDecimationFactor(int factor);
    int getDecimationFactor() const { return decimationFactor; }
    void setAGCAttack(float rate) {
        QMutexLocker lock(&m_mutex);
        agcAttackRate = rate;
    }
    void setAGCDecay(float rate) {
        QMutexLocker lock(&m_mutex);
        agcDecayRate = rate;
    }
    float getAGCAttack() const { return agcAttackRate; }
    float getAGCDecay() const { return agcDecayRate; }
    void setVSyncThreshold(float threshold) {
        QMutexLocker lock(&m_mutex);
        vSyncThreshold = threshold;
    }
    float getVSyncThreshold() const { return vSyncThreshold; }

    // FM deviation
    void setFMDeviation(double deviation) {
        QMutexLocker lock(&m_mutex);
        fmDeviation = deviation;
    }
    double getFMDeviation() const { return fmDeviation; }

    // Interlacing control
    void setDeinterlace(bool enable) {
        QMutexLocker lock(&m_mutex);
        enableDeinterlace = enable;
    }
    bool getDeinterlace() const { return enableDeinterlace; }

    // Video adjustment setters/getters
    void setVideoBrightness(float brightness) {
        QMutexLocker lock(&m_mutex);
        m_brightness = brightness;
    }
    void setVideoContrast(float contrast) {
        QMutexLocker lock(&m_mutex);
        m_contrast = contrast;
    }
    void setVideoGamma(float gamma) {
        QMutexLocker lock(&m_mutex);
        m_gamma = gamma;
    }
    float getVideoBrightness() const { return m_brightness; }
    float getVideoContrast() const { return m_contrast; }
    float getVideoGamma() const { return m_gamma; }

    // AM-specific controls
    void setAMScaleFactor(float factor) {
        QMutexLocker lock(&m_mutex);
        amScaleFactor = clamp(factor, 0.5f, 2.0f);
    }
    float getAMScaleFactor() const { return amScaleFactor; }

    void setAMLevelShift(float shift) {
        QMutexLocker lock(&m_mutex);
        amLevelShift = clamp(shift, -0.5f, 0.5f);
    }
    float getAMLevelShift() const { return amLevelShift; }

    void setBlackLevel(float level) {
        QMutexLocker lock(&m_mutex);
        blackLevelTarget = clamp(level, 0.2f, 0.4f);
    }
    float getBlackLevel() const { return blackLevelTarget; }

    // Vestigial sideband filter controls
    void setVSBFilterEnabled(bool enable) {
        QMutexLocker lock(&m_mutex);
        vsbFilterEnabled = enable;
    }
    bool getVSBFilterEnabled() const { return vsbFilterEnabled; }
    void setVSBUpperCutoff(double freq) {
        QMutexLocker lock(&m_mutex);
        vsbUpperCutoff = freq;
    }
    double getVSBUpperCutoff() const { return vsbUpperCutoff; }
    void setVSBLowerCutoff(double freq) {
        QMutexLocker lock(&m_mutex);
        vsbLowerCutoff = freq;
    }
    double getVSBLowerCutoff() const { return vsbLowerCutoff; }

    // Carrier tracking for AM
    void setCarrierTrackingEnabled(bool enable) {
        QMutexLocker lock(&m_mutex);
        carrierTrackingEnabled = enable;
    }
    bool getCarrierTrackingEnabled() const { return carrierTrackingEnabled; }

    // Sync status (DATV-style)
    bool isLineSynced() const { return m_lineSynced; }
    bool isFrameSynced() const { return vSyncLocked; }
    int getCurrentLine() const { return m_currentLine; }
    float getSyncLevel() const { return m_syncLevel; }

    // Signal quality
    double getMagSq() const { return m_magSqAverage; }

    // Reset to defaults
    void resetToDefaults();

private:
    // ========================================================================
    // PAL-B/G STANDARD CONSTANTS
    // ========================================================================
    static constexpr double PAL_LINE_DURATION = 64e-6;
    static constexpr double PAL_LINE_FREQUENCY = 15625.0;
    static constexpr double PAL_H_SYNC_DURATION = 4.7e-6;
    static constexpr double PAL_BACK_PORCH = 5.7e-6;
    static constexpr double PAL_FRONT_PORCH = 1.65e-6;
    static constexpr double PAL_ACTIVE_VIDEO = 51.95e-6;
    static constexpr double PAL_VSYNC_DURATION = 160e-6;

    static constexpr int PAL_TOTAL_LINES = 625;
    static constexpr int PAL_VISIBLE_LINES = 576;
    static constexpr int PAL_VBI_LINES_PER_FIELD = 25;
    static constexpr double PAL_FIELD_RATE = 50.0;
    static constexpr double PAL_FRAME_RATE = 25.0;

    static constexpr double AUDIO_CARRIER_OFFSET = 5.5e6;
    static constexpr double COLOR_SUBCARRIER = 4.43361875e6;

    static constexpr float SYNC_LEVEL = 0.0f;
    static constexpr float BLANKING_LEVEL = 0.3f;
    static constexpr float BLACK_LEVEL = 0.3f;
    static constexpr float WHITE_LEVEL = 1.0f;

    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================

    // DATV-style TVScreen integration
    TVScreen *m_tvScreen;
    QImage m_lastFrame;
    bool m_lineSynced;
    int m_currentLine;
    float m_syncLevel;
    double m_magSqAverage;

    // Thread safety (DATV-style)
    mutable QRecursiveMutex m_mutex;

    // Demodulation mode
    DemodMode demodMode = DEMOD_FM;
    bool invertVideo = false;

    // Sample rates
    double sampleRate;
    double effectiveSampleRate;

    // Carrier frequencies
    double videoCarrier = 0.0;
    double audioCarrier = AUDIO_CARRIER_OFFSET;
    double fmDeviation = 6.0e6;

    // Timing
    double lineDuration = PAL_LINE_DURATION;
    double horizontalOffset = 0.148;
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
    float m_brightness = 0.0f;
    float m_contrast = 1.0f;
    float m_gamma = 1.0f;

    // AM-specific parameters
    float amScaleFactor = 1.0f;
    float amLevelShift = 0.0f;
    float blackLevelTarget = 0.3f;
    float dcTrackingSpeed = 0.001f;
    float syncTipEstimate = 0.0f;

    // Vestigial sideband filter parameters
    bool vsbFilterEnabled = true;
    double vsbUpperCutoff = 5.5e6;
    double vsbLowerCutoff = 0.75e6;

    // Carrier tracking
    bool carrierTrackingEnabled = false;
    float carrierPhase = 0.0f;
    float carrierFreq = 0.0f;
    float carrierLoopAlpha = 0.001f;

    // Interlacing
    bool enableDeinterlace = false;
    int currentField = 0;

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

    // AM Demodulation with improvements
    std::vector<float> amDemodulate(
        const std::vector<std::complex<float>>& signal);

    std::vector<float> restoreDCForAM(
        const std::vector<float>& signal);

    std::vector<std::complex<float>> applyVestigialSidebandFilter(
        const std::vector<std::complex<float>>& signal);

    std::vector<std::complex<float>> trackCarrierAM(
        const std::vector<std::complex<float>>& signal);

    std::vector<std::complex<float>> complexLowPassFilterWithCoeffs(
        const std::vector<std::complex<float>>& signal,
        const std::vector<float>& coeffs);

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

    // FFT helpers (simplified - you may want to use a proper FFT library)
    std::vector<std::complex<float>> fft(const std::vector<std::complex<float>>& signal);
    std::vector<std::complex<float>> ifft(const std::vector<std::complex<float>>& spectrum);

    std::deque<float> amAgcHistory;
    size_t amAgcBufferSize = 0;
    float amAgcAverage = 1.0f;

    // Stable sync level tracking
    float syncLevelEstimate = 0.0f;
    float blackLevelEstimate = 0.3f;
    int frameCount = 0;

    float signalQuality = 0.0f;
    float syncToVideoRatio = 0.0f;
    int stableFrameCount = 0;

    // Sync pulse filtering
    std::vector<float> syncLevelHistory;
    size_t syncHistorySize = 100;

    // Expected timing (for sanity checks)
    size_t expectedFrameSamples = 0;
    size_t lastValidVSyncPos = 0;
};

#endif // PALBDEMODULATOR_H
