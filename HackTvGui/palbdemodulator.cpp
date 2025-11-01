#include "PALBDemodulator.h"
#include "tvscreen.h"
#include <algorithm>
#include <numeric>
#include <QPainter>
#include <QtMath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// For TVScreen compatibility
#ifndef SDR_RX_SCALED
#define SDR_RX_SCALED 32768.0
#endif

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================
PALBDemodulator::PALBDemodulator(double _sampleRate, QObject *parent)
    : QObject(parent)
    , m_tvScreen(nullptr)
    , m_lineSynced(false)
    , m_currentLine(0)
    , m_syncLevel(0.0f)
    , m_magSqAverage(0.0)
    , sampleRate(_sampleRate)
    , effectiveSampleRate(_sampleRate)
{
    calculateLineParameters();
    initializeFilters();
    resetToDefaults();

    qDebug() << "PALBDemodulator initialized:"
             << "SampleRate:" << sampleRate
             << "PointsPerLine:" << pointsPerLine
             << "SamplesPerLine:" << samplesPerLine;
}

PALBDemodulator::~PALBDemodulator()
{
}

// ============================================================================
// TVSCREEN RENDERING (DATV-STYLE)
// ============================================================================

void PALBDemodulator::renderToTVScreen()
{
    QMutexLocker lock(&m_mutex);

    if (!m_tvScreen) {
        qWarning() << "renderToTVScreen: m_tvScreen is null";
        return;
    }

    if (m_lastFrame.isNull()) {
        qWarning() << "renderToTVScreen: m_lastFrame is null";
        return;
    }

    int height = std::min(m_lastFrame.height(), visibleLines);
    int width = m_lastFrame.width();

    // Render frame to TVScreen line by line (DATV-style)
    for (int y = 0; y < height; y++) {
        m_tvScreen->selectRow(y);

        for (int x = 0; x < width; x++) {
            QRgb pixel = m_lastFrame.pixel(x, y);
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);
            m_tvScreen->setDataColor(x, r, g, b);
        }
    }
    m_tvScreen->renderImage(0);
}

// ============================================================================
// INITIALIZATION
// ============================================================================
void PALBDemodulator::resetToDefaults()
{
    QMutexLocker lock(&m_mutex);

    videoCarrier = 0.0;
    fmDeviation = 6.0e6;

    lineDuration = PAL_LINE_DURATION;
    horizontalOffset = (PAL_H_SYNC_DURATION + PAL_BACK_PORCH) / PAL_LINE_DURATION;

    pixelsPerLine = 720;
    visibleLines = 576;
    vbiLines = PAL_VBI_LINES_PER_FIELD;

    decimationFactor = 1;
    agcAttackRate = 0.001f;
    agcDecayRate = 0.0001f;
    vSyncThreshold = 0.15f;

    m_brightness = 0.0f;
    m_contrast = 1.0f;
    m_gamma = 1.0f;

    enableDeinterlace = false;

    agcLevel = 1.0f;
    peakLevel = 0.0f;
    vSyncLocked = false;
    lastPhase = 0.0f;

    m_lineSynced = false;
    m_currentLine = 0;
    m_syncLevel = 0.0f;
    m_magSqAverage = 0.0;

    // AM-specific defaults
    amScaleFactor = 1.0f;
    amLevelShift = 0.0f;
    blackLevelTarget = 0.3f;
    dcTrackingSpeed = 0.001f;
    syncTipEstimate = 0.0f;

    // VSB filter defaults
    vsbFilterEnabled = true;
    vsbUpperCutoff = 5.5e6;
    vsbLowerCutoff = 0.75e6;

    // Carrier tracking defaults
    carrierTrackingEnabled = false;
    carrierPhase = 0.0f;
    carrierFreq = 0.0f;
    carrierLoopAlpha = 0.001f;
}

void PALBDemodulator::setSampleRate(double rate)
{
    QMutexLocker lock(&m_mutex);
    sampleRate = rate;
    effectiveSampleRate = rate / decimationFactor;
    calculateLineParameters();
    initializeFilters();
}

void PALBDemodulator::setPixelsPerLine(int pixels)
{
    QMutexLocker lock(&m_mutex);
    pixelsPerLine = pixels;
    calculateLineParameters();
}

void PALBDemodulator::setDecimationFactor(int factor)
{
    QMutexLocker lock(&m_mutex);
    decimationFactor = std::max(1, factor);
    effectiveSampleRate = sampleRate / decimationFactor;
    calculateLineParameters();
    initializeFilters();
}

void PALBDemodulator::calculateLineParameters()
{
    effectiveSampleRate = sampleRate / decimationFactor;
    pointsPerLine = effectiveSampleRate / lineFrequency;
    samplesPerLine = static_cast<int>(pointsPerLine);
    fractionalOffset = pointsPerLine - samplesPerLine;

    lineBuffer.resize(samplesPerLine * 2);
}

void PALBDemodulator::initializeFilters()
{
    try {
        effectiveSampleRate = sampleRate / decimationFactor;

        int numTaps = 65;
        if (effectiveSampleRate > 10e6) {
            numTaps = 33;
        }

        float videoCutoff = std::min(5.5e6, effectiveSampleRate * 0.4);
        lowpassCoeffs = designLowPassFIR(numTaps, videoCutoff, effectiveSampleRate);

        if (lowpassCoeffs.empty()) {
            qWarning() << "Failed to design lowpass filter, using simple averaging";
            lowpassCoeffs = std::vector<float>(5, 0.2f);
        }

        float decimCutoff = effectiveSampleRate * 0.4f;
        videoFilterCoeffs = designLowPassFIR(numTaps, decimCutoff, sampleRate);

        if (videoFilterCoeffs.empty()) {
            videoFilterCoeffs = std::vector<float>(5, 0.2f);
        }

        complexFilterState.clear();
        complexFilterState.resize(numTaps, std::complex<float>(0, 0));

        realFilterState.clear();
        realFilterState.resize(numTaps, 0.0f);
    }
    catch (const std::exception& e) {
        qCritical() << "Failed to initialize filters:" << e.what();
        lowpassCoeffs = {0.25f, 0.5f, 0.25f};
        videoFilterCoeffs = {0.25f, 0.5f, 0.25f};
    }
}

// ============================================================================
// MAIN DEMODULATION FUNCTIONS
// ============================================================================
PALBDemodulator::DemodulatedFrame PALBDemodulator::demodulate(
    const std::vector<std::complex<float>>& samples)
{
    QMutexLocker lock(&m_mutex);

    DemodulatedFrame frame;

    if (samples.empty()) {
        frame.valid = false;
        return frame;
    }

    frame.image = demodulateVideoOnly(samples);

    // Store last frame for TVScreen rendering
    if (!frame.image.isNull()) {
        m_lastFrame = frame.image;
    }

    if (audioCarrier > 0) {
        frame.audio = demodulateAudioFM(samples);
    }

    frame.valid = !frame.image.isNull();
    frame.fieldNumber = currentField;

    return frame;
}

// PALBDemodulator.cpp - demodulateVideoOnly - ORİJİNAL signal conditioning'e geri dön

QImage PALBDemodulator::demodulateVideoOnly(
    const std::vector<std::complex<float>>& samples)
{
    if (samples.empty()) {
        qDebug() << "ERROR: Empty samples";
        return QImage();
    }

    try {
        // 1. Frequency shift to baseband
        std::vector<std::complex<float>> shifted;
        if (std::abs(videoCarrier) > 1.0) {
            shifted = frequencyShift(samples, -videoCarrier);
        } else {
            shifted = samples;
        }

        // 2a. Apply vestigial sideband filter for AM
        if (demodMode == DEMOD_AM && vsbFilterEnabled) {
            shifted = applyVestigialSidebandFilter(shifted);
        }

        // 2b. Apply carrier tracking for AM (optional)
        if (demodMode == DEMOD_AM && carrierTrackingEnabled) {
            shifted = trackCarrierAM(shifted);
        }

        // 2c. Pre-filter and decimate if needed
        if (decimationFactor > 1) {
            shifted = complexLowPassFilter(shifted, effectiveSampleRate * 0.45f);
            shifted = decimateComplex(shifted, decimationFactor);
        }

        // 3. Demodulation
        std::vector<float> demodulated;
        if (demodMode == DEMOD_AM) {
            demodulated = amDemodulate(shifted);
            // AM demodulate now handles proper scaling internally
        } else {
            demodulated = fmDemodulateAtan2(shifted);
        }

        if (demodulated.empty()) {
            qDebug() << "ERROR: Demodulation returned empty";
            return QImage();
        }       

        if (demodulated.empty()) {
            qDebug() << "ERROR: Lowpass filter returned empty";
            return QImage();
        }

        // 5. Apply signal conditioning based on mode - ORİJİNAL
        if (demodMode == DEMOD_AM) {
            // AM-specific DC restoration
            demodulated = restoreDCForAM(demodulated);
        } else {
            // FM signal conditioning
            demodulated = applyAGC(demodulated);
            demodulated = removeDCOffset(demodulated);
            demodulated = normalizeSignal(demodulated);
        }

        // 6. Detect vertical sync
        size_t vsyncPos = 0;
        int fieldType = 0;
        bool syncFound = detectVerticalSync(demodulated, vsyncPos, fieldType);

        if (syncFound) {
            vSyncLocked = true;
            m_lineSynced = true;
            m_syncLevel = demodulated[vsyncPos];
            vSyncCounter = 0;
        } else {
            if (vSyncCounter++ > 10) {
                vSyncLocked = false;
                m_lineSynced = false;
            }
        }

        // 7. Skip VBI if sync found
        if (syncFound && vsyncPos + vbiLines * samplesPerLine < demodulated.size()) {
            std::vector<float> videoOnly(
                demodulated.begin() + vsyncPos + vbiLines * samplesPerLine,
                demodulated.end()
                );
            demodulated = videoOnly;
        }

        // Video lowpass filter
        demodulated = lowPassFilter(demodulated, 5.5e6);

        // 8. Timing recovery
        demodulated = timingRecovery(demodulated);

        if (demodulated.empty()) {
            qDebug() << "ERROR: Timing recovery returned empty";
            return QImage();
        }

        // 9. Deinterlace if enabled
        if (enableDeinterlace && vSyncLocked) {
            demodulated = deinterlaceFields(demodulated);
        }

        // Update current line count
        m_currentLine = (demodulated.size() / pixelsPerLine) % PAL_TOTAL_LINES;

        // 10. Convert to image
        QImage image = convertToImage(demodulated, m_brightness, m_contrast);

        if (image.isNull()) {
            qDebug() << "ERROR: convertToImage returned null";
            return QImage();
        }

        // 11. Apply gamma correction
        if (std::abs(m_gamma - 1.0f) > 0.01f) {
            image = applyGammaCorrection(image, m_gamma);
        }

        // CRITICAL: Store frame for TVScreen rendering
        m_lastFrame = image;

        return image;
    }
    catch (const std::exception& e) {
        qCritical() << "EXCEPTION in demodulateVideoOnly:" << e.what();
        return QImage();
    }
}

std::vector<float> PALBDemodulator::demodulateAudioOnly(
    const std::vector<std::complex<float>>& samples)
{
    QMutexLocker lock(&m_mutex);

    if (samples.empty() || audioCarrier <= 0) {
        return std::vector<float>();
    }

    auto audioSignal = frequencyShift(samples, -audioCarrier);
    auto audio = fmDemodulateAtan2(audioSignal);
    audio = lowPassFilter(audio, 15000.0f);

    if (effectiveSampleRate > 48000) {
        int audioDecim = static_cast<int>(effectiveSampleRate / 48000);
        audio = decimate(audio, audioDecim);
    }

    return audio;
}

// ============================================================================
// FM DEMODULATION
// ============================================================================
std::vector<float> PALBDemodulator::fmDemodulateAtan2(
    const std::vector<std::complex<float>>& signal)
{
    if (signal.empty()) return std::vector<float>();

    std::vector<float> demod(signal.size());

    float maxMag = 0;
    for (size_t i = 0; i < std::min<size_t>(1000, signal.size()); i++) {
        float mag = std::abs(signal[i]);
        maxMag = std::max(maxMag, mag);
    }

    for (size_t i = 0; i < signal.size(); i++) {
        float phase = std::atan2(signal[i].imag(), signal[i].real());
        float delta = unwrapPhase(phase, lastPhase);

        demod[i] = delta * effectiveSampleRate / (2.0f * M_PI * fmDeviation);

        lastPhase = phase;
    }

    return demod;
}

std::vector<float> PALBDemodulator::fmDemodulateDifferential(
    const std::vector<std::complex<float>>& signal)
{
    if (signal.size() < 2) return std::vector<float>();

    std::vector<float> demod(signal.size());

    for (size_t i = 1; i < signal.size(); i++) {
        std::complex<float> conj = std::conj(signal[i-1]);
        std::complex<float> product = signal[i] * conj;

        float phase = std::atan2(product.imag(), product.real());
        demod[i] = phase * effectiveSampleRate / (2.0f * M_PI * fmDeviation);
    }
    demod[0] = demod[1];

    return demod;
}

// ============================================================================
// AM DEMODULATION
// ============================================================================

std::vector<float> PALBDemodulator::amDemodulate(
    const std::vector<std::complex<float>>& signal)
{
    if (signal.empty()) return std::vector<float>();

    std::vector<float> demod(signal.size());

    // Basic envelope detection
    for (size_t i = 0; i < signal.size(); i++) {
        demod[i] = std::abs(signal[i]);
    }

    // Find the sync tip (minimum) and peak white (maximum)
    auto minmax = std::minmax_element(demod.begin(), demod.end());
    float syncTip = *minmax.first;
    float peakWhite = *minmax.second;

    // Apply scale factor (like SDRangel's AM scale control)
    float range = (peakWhite - syncTip) * amScaleFactor;

    if (range < 0.001f) {
        // Prevent division by zero
        range = 1.0f;
    }

    // Normalize with proper black level positioning
    for (size_t i = 0; i < signal.size(); i++) {
        // Shift and scale to 0-1 range
        demod[i] = (demod[i] - syncTip) / range;

        // Apply level shift to position black level at ~0.3
        demod[i] = demod[i] + amLevelShift;

        // Clamp to valid range
        demod[i] = clamp(demod[i], 0.0f, 1.0f);
    }

    return demod;
}

std::vector<float> PALBDemodulator::restoreDCForAM(
    const std::vector<float>& signal)
{
    if (signal.empty()) return signal;

    std::vector<float> restored(signal.size());

    // Track sync tip level with exponential averaging
    for (size_t i = 0; i < signal.size(); i++) {
        // Update sync tip estimate during sync pulses
        if (signal[i] < 0.2f) { // Likely a sync pulse
            syncTipEstimate = syncTipEstimate * (1.0f - dcTrackingSpeed) +
                              signal[i] * dcTrackingSpeed;
        }

        // Restore DC by referencing to sync tip
        restored[i] = signal[i] - syncTipEstimate;

        // Scale so that the range from sync (0) to white (0.7) becomes 0 to 1
        // Black level should end up at approximately 0.3/0.7 = 0.43
        restored[i] = restored[i] / (1.0f - blackLevelTarget);

        // Adjust to position black level correctly
        if (restored[i] > blackLevelTarget) {
            // Scale video portion (above black)
            float videoRange = 1.0f - blackLevelTarget;
            restored[i] = blackLevelTarget +
                          (restored[i] - blackLevelTarget) * videoRange / 0.7f;
        }

        // Clamp
        restored[i] = clamp(restored[i], 0.0f, 1.0f);
    }

    // Apply inversion if needed for AM
    if (invertVideo) {
        for (auto& val : restored) {
            val = 1.0f - val;
        }
    }

    return restored;
}

std::vector<std::complex<float>> PALBDemodulator::applyVestigialSidebandFilter(
    const std::vector<std::complex<float>>& signal)
{
    if (signal.empty()) return signal;

    const int numTaps = 65;
    std::vector<float> vsbCoeffs(numTaps);

    float fc1 = vsbLowerCutoff / effectiveSampleRate;
    float fc2 = vsbUpperCutoff / effectiveSampleRate;

    int center = numTaps / 2;

    for (int i = 0; i < numTaps; i++) {
        if (i == center) {
            vsbCoeffs[i] = 2.0f * (fc2 + fc1);
        } else {
            float n = i - center;
            float h1 = std::sin(2.0f * M_PI * fc2 * n) / (M_PI * n);
            float h2 = std::sin(2.0f * M_PI * fc1 * n) / (M_PI * n);
            vsbCoeffs[i] = h1 + h2 * 0.5f;
        }

        float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (numTaps - 1));
        vsbCoeffs[i] *= window;
    }

    float sum = std::accumulate(vsbCoeffs.begin(), vsbCoeffs.end(), 0.0f);
    if (sum != 0) {
        for (auto& c : vsbCoeffs) c /= sum;
    }

    return complexLowPassFilterWithCoeffs(signal, vsbCoeffs);
}

std::vector<std::complex<float>> PALBDemodulator::trackCarrierAM(
    const std::vector<std::complex<float>>& signal)
{
    if (signal.empty()) return signal;

    std::vector<std::complex<float>> tracked(signal.size());

    for (size_t i = 0; i < signal.size(); i++) {
        std::complex<float> lo(cos(carrierPhase), sin(carrierPhase));
        tracked[i] = signal[i] * std::conj(lo);

        float error = std::atan2(tracked[i].imag(), tracked[i].real());

        carrierFreq += carrierLoopAlpha * error;
        carrierPhase += carrierFreq;

        while (carrierPhase > M_PI) carrierPhase -= 2.0f * M_PI;
        while (carrierPhase < -M_PI) carrierPhase += 2.0f * M_PI;
    }

    return tracked;
}

std::vector<std::complex<float>> PALBDemodulator::complexLowPassFilterWithCoeffs(
    const std::vector<std::complex<float>>& signal,
    const std::vector<float>& coeffs)
{
    if (signal.empty() || coeffs.empty()) {
        return std::vector<std::complex<float>>();
    }

    std::vector<std::complex<float>> filtered(signal.size());
    int halfTaps = coeffs.size() / 2;

    for (size_t i = 0; i < signal.size(); i++) {
        std::complex<float> sum(0, 0);

        for (size_t j = 0; j < coeffs.size(); j++) {
            int idx = i - halfTaps + j;
            if (idx >= 0 && idx < static_cast<int>(signal.size())) {
                sum += signal[idx] * coeffs[j];
            }
        }

        filtered[i] = sum;
    }

    return filtered;
}

// ============================================================================
// FREQUENCY OPERATIONS
// ============================================================================
std::vector<std::complex<float>> PALBDemodulator::frequencyShift(
    const std::vector<std::complex<float>>& signal,
    double shiftFreq)
{
    if (std::abs(shiftFreq) < 1.0) return signal;

    std::vector<std::complex<float>> shifted(signal.size());
    double phaseInc = 2.0 * M_PI * shiftFreq / sampleRate;
    double phase = 0.0;

    for (size_t i = 0; i < signal.size(); i++) {
        std::complex<float> shift(std::cos(phase), std::sin(phase));
        shifted[i] = signal[i] * shift;
        phase += phaseInc;

        if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
        if (phase < -2.0 * M_PI) phase += 2.0 * M_PI;
    }

    return shifted;
}

// ============================================================================
// FILTERING
// ============================================================================
std::vector<float> PALBDemodulator::designLowPassFIR(
    int numTaps,
    float cutoffFreq,
    float sampleRate)
{
    std::vector<float> coeffs(numTaps);
    float fc = cutoffFreq / sampleRate;
    int center = numTaps / 2;

    for (int i = 0; i < numTaps; i++) {
        if (i == center) {
            coeffs[i] = 2.0f * fc;
        } else {
            float n = i - center;
            coeffs[i] = std::sin(2.0f * M_PI * fc * n) / (M_PI * n);
        }

        float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (numTaps - 1));
        coeffs[i] *= window;
    }

    float sum = std::accumulate(coeffs.begin(), coeffs.end(), 0.0f);
    if (sum != 0) {
        for (auto& c : coeffs) c /= sum;
    }

    return coeffs;
}

std::vector<std::complex<float>> PALBDemodulator::complexLowPassFilter(
    const std::vector<std::complex<float>>& signal,
    float cutoffFreq)
{
    if (signal.empty()) return signal;

    if (signal.size() > 10000000) {
        qCritical() << "Signal too large for filtering:" << signal.size();
        return std::vector<std::complex<float>>();
    }

    try {
        std::vector<float> coeffs;
        if (cutoffFreq <= 6e6 && !lowpassCoeffs.empty()) {
            coeffs = lowpassCoeffs;
        } else {
            int numTaps = 65;
            if (signal.size() > 500000) {
                numTaps = 33;
            }
            coeffs = designLowPassFIR(numTaps, cutoffFreq, effectiveSampleRate);
        }

        if (coeffs.empty()) {
            qCritical() << "Failed to design filter coefficients";
            return signal;
        }

        const size_t CHUNK_SIZE = 100000;

        if (signal.size() > CHUNK_SIZE * 2) {
            std::vector<std::complex<float>> filtered;
            filtered.reserve(signal.size());

            size_t halfTaps = coeffs.size() / 2;

            for (size_t chunkStart = 0; chunkStart < signal.size(); chunkStart += CHUNK_SIZE) {
                size_t chunkEnd = std::min(chunkStart + CHUNK_SIZE + coeffs.size(), signal.size());
                size_t actualStart = (chunkStart > halfTaps) ? chunkStart - halfTaps : 0;

                std::vector<std::complex<float>> chunk(
                    signal.begin() + actualStart,
                    signal.begin() + chunkEnd
                    );

                std::vector<std::complex<float>> chunkFiltered(chunk.size());
                for (size_t i = 0; i < chunk.size(); i++) {
                    std::complex<float> sum(0, 0);

                    for (size_t j = 0; j < coeffs.size(); j++) {
                        int idx = i - halfTaps + j;
                        if (idx >= 0 && idx < static_cast<int>(chunk.size())) {
                            sum += chunk[idx] * coeffs[j];
                        }
                    }

                    chunkFiltered[i] = sum;
                }

                size_t copyStart = (chunkStart > 0) ? halfTaps : 0;
                size_t copyEnd = std::min(copyStart + CHUNK_SIZE, chunkFiltered.size());

                filtered.insert(filtered.end(),
                                chunkFiltered.begin() + copyStart,
                                chunkFiltered.begin() + copyEnd);
            }

            return filtered;
        } else {
            std::vector<std::complex<float>> filtered(signal.size());
            int halfTaps = coeffs.size() / 2;

            for (size_t i = 0; i < signal.size(); i++) {
                std::complex<float> sum(0, 0);

                for (size_t j = 0; j < coeffs.size(); j++) {
                    int idx = i - halfTaps + j;
                    if (idx >= 0 && idx < static_cast<int>(signal.size())) {
                        sum += signal[idx] * coeffs[j];
                    }
                }

                filtered[i] = sum;
            }

            return filtered;
        }
    }
    catch (const std::bad_alloc& e) {
        qCritical() << "Memory allocation failed in complexLowPassFilter:" << e.what();
        std::vector<std::complex<float>> decimated;
        for (size_t i = 0; i < signal.size(); i += 2) {
            decimated.push_back(signal[i]);
        }
        return decimated;
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in complexLowPassFilter:" << e.what();
        return signal;
    }
}

std::vector<float> PALBDemodulator::applyFIRFilter(
    const std::vector<float>& signal,
    const std::vector<float>& coeffs)
{
    if (signal.empty() || coeffs.empty()) {
        return std::vector<float>();
    }

    const size_t signalSize = signal.size();
    const size_t filterSize = coeffs.size();
    const int halfTaps = filterSize / 2;

    std::vector<float> filtered(signalSize);

    for (size_t i = 0; i < halfTaps && i < signalSize; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < filterSize; j++) {
            int idx = i - halfTaps + j;
            if (idx >= 0 && idx < static_cast<int>(signalSize)) {
                sum += signal[idx] * coeffs[j];
            }
        }
        filtered[i] = sum;
    }

    for (size_t i = halfTaps; i < signalSize - halfTaps; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < filterSize; j++) {
            sum += signal[i - halfTaps + j] * coeffs[j];
        }
        filtered[i] = sum;
    }

    for (size_t i = signalSize - halfTaps; i < signalSize; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < filterSize; j++) {
            int idx = i - halfTaps + j;
            if (idx >= 0 && idx < static_cast<int>(signalSize)) {
                sum += signal[idx] * coeffs[j];
            }
        }
        filtered[i] = sum;
    }

    return filtered;
}

std::vector<float> PALBDemodulator::lowPassFilter(
    const std::vector<float>& signal,
    float cutoffFreq)
{
    if (signal.empty()) return signal;

    if (signal.size() > 10000000) {
        qCritical() << "Signal too large for filtering:" << signal.size();
        return std::vector<float>();
    }

    try {
        std::vector<float> coeffs = designLowPassFIR(65, cutoffFreq, effectiveSampleRate);
        return applyFIRFilter(signal, coeffs);
    }
    catch (const std::exception& e) {
        qCritical() << "Filter error:" << e.what();
        return std::vector<float>();
    }
}

// ============================================================================
// DECIMATION
// ============================================================================
std::vector<float> PALBDemodulator::decimate(
    const std::vector<float>& signal,
    int factor)
{
    if (factor <= 1) return signal;

    std::vector<float> decimated;
    decimated.reserve(signal.size() / factor);

    for (size_t i = 0; i < signal.size(); i += factor) {
        decimated.push_back(signal[i]);
    }

    return decimated;
}

std::vector<std::complex<float>> PALBDemodulator::decimateComplex(
    const std::vector<std::complex<float>>& signal,
    int factor)
{
    if (factor <= 1) return signal;

    std::vector<std::complex<float>> decimated;
    decimated.reserve(signal.size() / factor);

    for (size_t i = 0; i < signal.size(); i += factor) {
        decimated.push_back(signal[i]);
    }

    return decimated;
}

// ============================================================================
// SIGNAL CONDITIONING
// ============================================================================
std::vector<float> PALBDemodulator::removeDCOffset(
    const std::vector<float>& signal)
{
    if (signal.empty()) return signal;

    float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) / signal.size();

    std::vector<float> dcRemoved(signal.size());
    std::transform(signal.begin(), signal.end(), dcRemoved.begin(),
                   [mean](float x) { return x - mean; });

    return dcRemoved;
}

std::vector<float> PALBDemodulator::applyAGC(
    const std::vector<float>& signal)
{
    if (signal.empty()) return signal;

    std::vector<float> agcSignal(signal.size());
    const float targetLevel = 0.7f;

    for (size_t i = 0; i < signal.size(); i++) {
        float absSample = std::abs(signal[i]);

        peakLevel = std::max(peakLevel * 0.9999f, absSample);

        float error = targetLevel - peakLevel;

        if (error < 0) {
            agcLevel *= (1.0f - agcAttackRate);
        } else {
            agcLevel *= (1.0f + agcDecayRate);
        }

        agcLevel = clamp(agcLevel, 0.1f, 10.0f);

        agcSignal[i] = signal[i] * agcLevel;
    }

    return agcSignal;
}

std::vector<float> PALBDemodulator::normalizeSignal(
    const std::vector<float>& signal)
{
    if (signal.empty()) return signal;

    auto minmax = std::minmax_element(signal.begin(), signal.end());
    float syncTip = *minmax.first;
    float peakWhite = *minmax.second;

    float range = peakWhite - syncTip;
    if (range < 0.001f) return signal;

    std::vector<float> normalized(signal.size());

    for (size_t i = 0; i < signal.size(); i++) {
        normalized[i] = (signal[i] - syncTip) / range;
        normalized[i] = clamp(normalized[i], 0.0f, 1.0f);
    }

    if (invertVideo) {
        for (auto& val : normalized) {
            val = 1.0f - val;
        }
    }

    return normalized;
}

// ============================================================================
// SYNCHRONIZATION
// ============================================================================

bool PALBDemodulator::detectVerticalSync(
    const std::vector<float>& signal,
    size_t& syncStart,
    int& fieldType)
{
    const float SYNC_THRESHOLD = vSyncThreshold;
    const int vsyncSamples = static_cast<int>(PAL_VSYNC_DURATION * effectiveSampleRate);
    const int minVSyncWidth = vsyncSamples * 0.3; // 0.5'den 0.3'e düşür - daha toleranslı

    // İki geçişli algoritma: önce uzun sync pulse'ları ara
    int syncCount = 0;
    size_t bestSyncPos = 0;
    int maxSyncCount = 0;
    bool inSync = false;

    for (size_t i = 0; i < signal.size(); i++) {
        if (signal[i] < SYNC_THRESHOLD) {
            if (!inSync) {
                // Yeni sync pulse başlangıcı
                inSync = true;
                syncCount = 1;
            } else {
                syncCount++;
            }

            if (syncCount > maxSyncCount) {
                maxSyncCount = syncCount;
                bestSyncPos = i - syncCount + 1;
            }
        } else {
            if (inSync && syncCount >= minVSyncWidth) {
                // Vertical sync bulundu!
                syncStart = bestSyncPos;
                fieldType = (bestSyncPos % (samplesPerLine * 2)) > samplesPerLine ? 1 : 0;
                lastVSyncPosition = bestSyncPos;
                vSyncCounter = 0;
                return true;
            }
            inSync = false;
            syncCount = 0;
        }
    }

    // Eğer en uzun sync pulse yeterince uzunsa onu kabul et
    if (maxSyncCount >= minVSyncWidth) {
        syncStart = bestSyncPos;
        fieldType = (bestSyncPos % (samplesPerLine * 2)) > samplesPerLine ? 1 : 0;
        lastVSyncPosition = bestSyncPos;
        vSyncCounter = 0;
        return true;
    }

    return false;
}

bool PALBDemodulator::detectHorizontalSync(
    const std::vector<float>& signal,
    size_t startPos,
    size_t& syncPos)
{
    const float SYNC_THRESHOLD = vSyncThreshold * 1.5f;
    const int hsyncSamples = static_cast<int>(PAL_H_SYNC_DURATION * effectiveSampleRate);
    const int searchWindow = samplesPerLine + samplesPerLine / 4;

    size_t endPos = std::min(startPos + searchWindow, signal.size());

    int syncCount = 0;
    int maxSyncCount = 0;
    size_t bestSyncPos = 0;

    for (size_t i = startPos; i < endPos; i++) {
        if (signal[i] < SYNC_THRESHOLD) {
            syncCount++;
            if (syncCount > maxSyncCount) {
                maxSyncCount = syncCount;
                bestSyncPos = i - syncCount + 1;
            }
        } else {
            if (syncCount >= hsyncSamples * 0.5) {
                syncPos = i - syncCount;
                return true;
            }
            syncCount = 0;
        }
    }

    if (maxSyncCount >= hsyncSamples * 0.3) {
        syncPos = bestSyncPos;
        return true;
    }

    return false;
}

std::vector<float> PALBDemodulator::removeVBI(
    const std::vector<float>& signal)
{
    size_t skipSamples = vbiLines * samplesPerLine;

    if (signal.size() > skipSamples) {
        return std::vector<float>(signal.begin() + skipSamples, signal.end());
    }

    return signal;
}

// ============================================================================
// TIMING RECOVERY
// ============================================================================
std::vector<float> PALBDemodulator::timingRecovery(
    const std::vector<float>& signal)
{
    if (signal.empty()) {
        qDebug() << "timingRecovery: empty input";
        return signal;
    }

    if (signal.size() < samplesPerLine * 2) {
        qDebug() << "timingRecovery: signal too small, returning as-is";
        return signal;
    }

    std::vector<float> recovered;
    recovered.reserve(signal.size());

    size_t pos = 0;
    float fractionalPos = 0.0f;
    int linesProcessed = 0;
    int syncFoundCount = 0;
    bool useFallbackMode = false;

    while (pos < signal.size() - samplesPerLine) {
        size_t syncPos;

        bool syncFound = detectHorizontalSync(signal, pos, syncPos);

        if (syncFound) {
            syncFoundCount++;

            size_t videoStart = syncPos + static_cast<size_t>(
                                    (PAL_H_SYNC_DURATION + PAL_BACK_PORCH) * effectiveSampleRate
                                    );

            if (videoStart + pixelsPerLine < signal.size()) {
                auto line = interpolateLine(signal, videoStart, pixelsPerLine);
                recovered.insert(recovered.end(), line.begin(), line.end());
                linesProcessed++;
            }

            pos = syncPos + samplesPerLine;
            fractionalPos += fractionalOffset;

            if (fractionalPos >= 1.0f) {
                pos += static_cast<size_t>(fractionalPos);
                fractionalPos -= static_cast<int>(fractionalPos);
            }
        } else {
            useFallbackMode = true;
            break;
        }

        if (linesProcessed > 10 && syncFoundCount < 5) {
            useFallbackMode = true;
            break;
        }
    }

    if (useFallbackMode || recovered.empty()) {
        recovered.clear();
        recovered.reserve(signal.size());

        size_t skipSamples = static_cast<size_t>(horizontalOffset * samplesPerLine);
        pos = skipSamples;

        while (pos + pixelsPerLine < signal.size()) {
            for (int i = 0; i < pixelsPerLine && pos + i < signal.size(); i++) {
                recovered.push_back(signal[pos + i]);
            }

            pos += samplesPerLine;
            fractionalPos += fractionalOffset;

            if (fractionalPos >= 1.0f) {
                pos += static_cast<size_t>(fractionalPos);
                fractionalPos -= static_cast<int>(fractionalPos);
            }

            linesProcessed++;
        }
    }

    return recovered;
}

std::vector<float> PALBDemodulator::interpolateLine(
    const std::vector<float>& signal,
    size_t startPos,
    int targetSamples)
{
    std::vector<float> line(targetSamples);

    size_t sourceSamples = std::min(
        static_cast<size_t>(targetSamples),
        signal.size() - startPos
        );

    if (sourceSamples == static_cast<size_t>(targetSamples)) {
        std::copy(signal.begin() + startPos,
                  signal.begin() + startPos + targetSamples,
                  line.begin());
    } else {
        float ratio = static_cast<float>(sourceSamples - 1) / (targetSamples - 1);

        for (int i = 0; i < targetSamples; i++) {
            float srcIdx = i * ratio;
            int idx0 = static_cast<int>(srcIdx);
            int idx1 = std::min(idx0 + 1, static_cast<int>(sourceSamples - 1));
            float frac = srcIdx - idx0;

            if (startPos + idx1 < signal.size()) {
                line[i] = signal[startPos + idx0] * (1.0f - frac) +
                          signal[startPos + idx1] * frac;
            }
        }
    }

    return line;
}

// ============================================================================
// FIELD PROCESSING
// ============================================================================
std::vector<float> PALBDemodulator::extractSingleField(
    const std::vector<float>& signal,
    bool oddField)
{
    std::vector<float> field;
    int lineStart = oddField ? 1 : 0;

    for (size_t line = lineStart; line < signal.size() / pixelsPerLine; line += 2) {
        size_t start = line * pixelsPerLine;
        size_t end = std::min(start + pixelsPerLine, signal.size());

        if (end > start) {
            field.insert(field.end(),
                         signal.begin() + start,
                         signal.begin() + end);
        }
    }

    return field;
}

std::vector<float> PALBDemodulator::deinterlaceFields(
    const std::vector<float>& signal)
{
    std::vector<float> deinterlaced;
    deinterlaced.reserve(signal.size() * 2);

    size_t numLines = signal.size() / pixelsPerLine;

    for (size_t line = 0; line < numLines; line++) {
        size_t start = line * pixelsPerLine;
        size_t end = std::min(start + pixelsPerLine, signal.size());

        for (int repeat = 0; repeat < 2; repeat++) {
            deinterlaced.insert(deinterlaced.end(),
                                signal.begin() + start,
                                signal.begin() + end);
        }
    }

    return deinterlaced;
}

// ============================================================================
// IMAGE CONVERSION
// ============================================================================
QImage PALBDemodulator::convertToImage(
    const std::vector<float>& videoSignal,
    float brightness,
    float contrast)
{
    if (videoSignal.empty()) {
        qDebug() << "ERROR: Empty video signal";
        return QImage();
    }

    int width = pixelsPerLine;
    int height = visibleLines;

    size_t availableLines = videoSignal.size() / pixelsPerLine;

    if (availableLines < 100) {
        qDebug() << "ERROR: Too few lines available:" << availableLines;
        return QImage();
    }

    height = std::min(height, static_cast<int>(availableLines));

    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull()) {
        qDebug() << "ERROR: Failed to create QImage";
        return QImage();
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t idx = y * width + x;

            if (idx < videoSignal.size()) {
                float value = videoSignal[idx];
                value = (value - 0.5f) * contrast + 0.5f + brightness;
                value = std::max(0.0f, std::min(1.0f, value));

                uint8_t gray = static_cast<uint8_t>(value * 255.0f);
                image.setPixel(x, y, qRgb(gray, gray, gray));
            }
        }
    }

    return image;
}

QImage PALBDemodulator::applyGammaCorrection(const QImage& image, float gamma)
{
    if (image.isNull() || gamma <= 0) {
        return image;
    }

    QImage corrected = image.copy();

    std::vector<uint8_t> lut(256);
    float invGamma = 1.0f / gamma;

    for (int i = 0; i < 256; i++) {
        float normalized = i / 255.0f;
        float correctedVal = std::pow(normalized, invGamma);
        lut[i] = floatToUint8(correctedVal);
    }

    for (int y = 0; y < corrected.height(); y++) {
        for (int x = 0; x < corrected.width(); x++) {
            QRgb pixel = corrected.pixel(x, y);
            int gray = qGray(pixel);
            uint8_t newGray = lut[gray];
            corrected.setPixel(x, y, qRgb(newGray, newGray, newGray));
        }
    }

    return corrected;
}

// ============================================================================
// AUDIO PROCESSING
// ============================================================================
std::vector<float> PALBDemodulator::demodulateAudioFM(
    const std::vector<std::complex<float>>& signal)
{
    auto audioShifted = frequencyShift(signal, -AUDIO_CARRIER_OFFSET);
    auto audio = fmDemodulateAtan2(audioShifted);
    audio = lowPassFilter(audio, 15000.0f);

    if (effectiveSampleRate > 48000) {
        int decimFactor = static_cast<int>(effectiveSampleRate / 48000);
        audio = decimate(audio, decimFactor);
    }

    return audio;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================
float PALBDemodulator::clamp(float value, float min, float max)
{
    return std::max(min, std::min(max, value));
}

uint8_t PALBDemodulator::floatToUint8(float value)
{
    int intVal = static_cast<int>(value * 255.0f + 0.5f);
    return static_cast<uint8_t>(clamp(intVal, 0, 255));
}

float PALBDemodulator::unwrapPhase(float phase, float lastPhase)
{
    float delta = phase - lastPhase;

    while (delta > M_PI) delta -= 2.0f * M_PI;
    while (delta < -M_PI) delta += 2.0f * M_PI;

    return delta;
}

std::vector<std::complex<float>> PALBDemodulator::fft(
    const std::vector<std::complex<float>>& signal)
{
    return signal;
}

std::vector<std::complex<float>> PALBDemodulator::ifft(
    const std::vector<std::complex<float>>& spectrum)
{
    return spectrum;
}
