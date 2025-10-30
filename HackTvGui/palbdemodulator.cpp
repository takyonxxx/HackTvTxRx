#include "PALBDemodulator.h"
#include <algorithm>
#include <numeric>
#include <QPainter>
#include <QtMath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================
PALBDemodulator::PALBDemodulator(double _sampleRate, QObject *parent)
    : QObject(parent)
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
// INITIALIZATION
// ============================================================================
void PALBDemodulator::resetToDefaults()
{
    videoCarrier = 0.0;  // Baseband
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
}

void PALBDemodulator::setSampleRate(double rate)
{
    sampleRate = rate;
    effectiveSampleRate = rate / decimationFactor;
    calculateLineParameters();
    initializeFilters();
}

void PALBDemodulator::setPixelsPerLine(int pixels)
{
    pixelsPerLine = pixels;
    calculateLineParameters();
}

void PALBDemodulator::setDecimationFactor(int factor)
{
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

    // Reserve line buffer
    lineBuffer.resize(samplesPerLine * 2);
}

void PALBDemodulator::initializeFilters()
{
    try {
        effectiveSampleRate = sampleRate / decimationFactor;

        // Limit filter size based on sample rate
        int numTaps = 65;
        if (effectiveSampleRate > 10e6) {
            numTaps = 33;  // Reduce for high sample rates
        }

        // Design lowpass filter for video (5.5 MHz cutoff)
        float videoCutoff = std::min(5.5e6, effectiveSampleRate * 0.4);
        lowpassCoeffs = designLowPassFIR(numTaps, videoCutoff, effectiveSampleRate);

        if (lowpassCoeffs.empty()) {
            qWarning() << "Failed to design lowpass filter, using simple averaging";
            lowpassCoeffs = std::vector<float>(5, 0.2f);  // Simple 5-tap averaging filter
        }

        // Design filter for decimation anti-aliasing
        float decimCutoff = effectiveSampleRate * 0.4f;
        videoFilterCoeffs = designLowPassFIR(numTaps, decimCutoff, sampleRate);

        if (videoFilterCoeffs.empty()) {
            videoFilterCoeffs = std::vector<float>(5, 0.2f);  // Fallback
        }

        // Pre-allocate filter states with reasonable sizes
        complexFilterState.clear();
        complexFilterState.resize(numTaps, std::complex<float>(0, 0));

        realFilterState.clear();
        realFilterState.resize(numTaps, 0.0f);
    }
    catch (const std::exception& e) {
        qCritical() << "Failed to initialize filters:" << e.what();
        // Use minimal filters as fallback
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
    DemodulatedFrame frame;

    if (samples.empty()) {
        frame.valid = false;
        return frame;
    }

    // Demodulate video
    frame.image = demodulateVideoOnly(samples);

    // Demodulate audio if needed
    if (audioCarrier > 0) {
        frame.audio = demodulateAudioFM(samples);
    }

    frame.valid = !frame.image.isNull();
    frame.fieldNumber = currentField;

    return frame;
}

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

        // 2. Pre-filter and decimate if needed
        if (decimationFactor > 1) {
            shifted = complexLowPassFilter(shifted, effectiveSampleRate * 0.45f);
            shifted = decimateComplex(shifted, decimationFactor);
        }

        // 3. FM demodulation
        std::vector<float> demodulated;
        if (demodMode == DEMOD_AM) {
            demodulated = amDemodulate(shifted);
        } else {
            demodulated = fmDemodulateAtan2(shifted);
        }
        if (demodulated.empty()) {
            qDebug() << "ERROR: FM demodulation returned empty";
            return QImage();
        }

        // Check for NaN or Inf values
        int nanCount = 0;
        for (size_t i = 0; i < std::min<size_t>(100, demodulated.size()); i++) {
            if (std::isnan(demodulated[i]) || std::isinf(demodulated[i])) {
                nanCount++;
            }
        }
        if (nanCount > 0) {
            qDebug() << "WARNING: Found" << nanCount << "NaN/Inf values in first 100 samples";
        }

        // 4. Video lowpass filter
        demodulated = lowPassFilter(demodulated, 5.5e6);

        if (demodulated.empty()) {
            qDebug() << "ERROR: Lowpass filter returned empty";
            return QImage();
        }

        // 5. Apply AGC
        demodulated = applyAGC(demodulated);
        demodulated = removeDCOffset(demodulated);
        demodulated = normalizeSignal(demodulated);

        // 7. Detect vertical sync
        size_t vsyncPos = 0;
        int fieldType = 0;
        bool syncFound = detectVerticalSync(demodulated, vsyncPos, fieldType);

        // 8. Skip VBI if sync found
        if (syncFound && vsyncPos + vbiLines * samplesPerLine < demodulated.size()) {
            std::vector<float> videoOnly(
                demodulated.begin() + vsyncPos + vbiLines * samplesPerLine,
                demodulated.end()
                );
            demodulated = videoOnly;
        }

        // 9. Timing recovery
        demodulated = timingRecovery(demodulated);

        if (demodulated.empty()) {
            qDebug() << "ERROR: Timing recovery returned empty";
            return QImage();
        }

        // 10. Deinterlace if enabled
        if (enableDeinterlace && vSyncLocked) {
            demodulated = deinterlaceFields(demodulated);
        }

        QImage image = convertToImage(demodulated, m_brightness, m_contrast);

        if (image.isNull()) {
            qDebug() << "ERROR: convertToImage returned null";
            return QImage();
        }

        // 12. Apply gamma correction
        if (std::abs(m_gamma - 1.0f) > 0.01f) {
            image = applyGammaCorrection(image, m_gamma);
        }
        return image;
    }
    catch (const std::exception& e) {
        qCritical() << "EXCEPTION in demodulateVideoOnly:" << e.what();
        return QImage();
    }
    catch (...) {
        qCritical() << "UNKNOWN EXCEPTION in demodulateVideoOnly";
        return QImage();
    }
}

std::vector<float> PALBDemodulator::demodulateAudioOnly(
    const std::vector<std::complex<float>>& samples)
{
    if (samples.empty() || audioCarrier <= 0) {
        return std::vector<float>();
    }

    // Shift to audio carrier frequency
    auto audioSignal = frequencyShift(samples, -audioCarrier);

    // FM demodulate
    auto audio = fmDemodulateAtan2(audioSignal);

    // Audio lowpass filter (15 kHz)
    audio = lowPassFilter(audio, 15000.0f);

    // Decimate to audio rate if needed
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

    // Debug: Check signal amplitude
    float maxMag = 0;
    for (size_t i = 0; i < std::min<size_t>(1000, signal.size()); i++) {
        float mag = std::abs(signal[i]);
        maxMag = std::max(maxMag, mag);
    }

    for (size_t i = 0; i < signal.size(); i++) {
        float phase = std::atan2(signal[i].imag(), signal[i].real());
        float delta = unwrapPhase(phase, lastPhase);

        // Try different scaling for FM deviation
        // Standard PAL uses different deviation on UHF
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
        // Differential phase detection
        std::complex<float> conj = std::conj(signal[i-1]);
        std::complex<float> product = signal[i] * conj;

        // Extract phase difference
        float phase = std::atan2(product.imag(), product.real());

        // Scale to frequency
        demod[i] = phase * effectiveSampleRate / (2.0f * M_PI * fmDeviation);
    }
    demod[0] = demod[1];  // Fill first sample

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

    // Basic envelope detection - this is correct!
    for (size_t i = 0; i < signal.size(); i++) {
        demod[i] = std::abs(signal[i]);
    }

    return demod;
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

        // Wrap phase to prevent numerical issues
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
    float fc = cutoffFreq / sampleRate;  // Normalized frequency
    int center = numTaps / 2;

    for (int i = 0; i < numTaps; i++) {
        if (i == center) {
            coeffs[i] = 2.0f * fc;
        } else {
            float n = i - center;
            coeffs[i] = std::sin(2.0f * M_PI * fc * n) / (M_PI * n);
        }

        // Apply Hamming window
        float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (numTaps - 1));
        coeffs[i] *= window;
    }

    // Normalize
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

    // Check for unreasonably large signals
    if (signal.size() > 10000000) {
        qCritical() << "Signal too large for filtering:" << signal.size();
        return std::vector<std::complex<float>>();
    }

    try {
        // Use pre-calculated coefficients if available
        std::vector<float> coeffs;
        if (cutoffFreq <= 6e6 && !lowpassCoeffs.empty()) {
            coeffs = lowpassCoeffs;
        } else {
            // Reduce filter taps for large signals to save memory
            int numTaps = 65;
            if (signal.size() > 500000) {
                numTaps = 33;  // Use fewer taps for large signals
            }
            coeffs = designLowPassFIR(numTaps, cutoffFreq, effectiveSampleRate);
        }

        if (coeffs.empty()) {
            qCritical() << "Failed to design filter coefficients";
            return signal;  // Return unfiltered rather than crash
        }

        // Process in chunks if signal is very large
        const size_t CHUNK_SIZE = 100000;  // Process 100k samples at a time

        if (signal.size() > CHUNK_SIZE * 2) {
            std::vector<std::complex<float>> filtered;
            filtered.reserve(signal.size());

            size_t halfTaps = coeffs.size() / 2;

            // Process in overlapping chunks
            for (size_t chunkStart = 0; chunkStart < signal.size(); chunkStart += CHUNK_SIZE) {
                size_t chunkEnd = std::min(chunkStart + CHUNK_SIZE + coeffs.size(), signal.size());
                size_t actualStart = (chunkStart > halfTaps) ? chunkStart - halfTaps : 0;

                // Extract chunk with overlap for filter continuity
                std::vector<std::complex<float>> chunk(
                    signal.begin() + actualStart,
                    signal.begin() + chunkEnd
                    );

                // Filter the chunk
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

                // Add the filtered chunk (skip overlap regions)
                size_t copyStart = (chunkStart > 0) ? halfTaps : 0;
                size_t copyEnd = std::min(copyStart + CHUNK_SIZE, chunkFiltered.size());

                filtered.insert(filtered.end(),
                                chunkFiltered.begin() + copyStart,
                                chunkFiltered.begin() + copyEnd);
            }

            return filtered;
        } else {
            // Small signal - process normally
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
        // Return a decimated version instead of crashing
        std::vector<std::complex<float>> decimated;
        for (size_t i = 0; i < signal.size(); i += 2) {
            decimated.push_back(signal[i]);
        }
        return decimated;
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in complexLowPassFilter:" << e.what();
        return signal;  // Return unfiltered
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

    // Process the beginning (partial convolution)
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

    // Process the middle (full convolution - no boundary checks needed)
    for (size_t i = halfTaps; i < signalSize - halfTaps; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < filterSize; j++) {
            sum += signal[i - halfTaps + j] * coeffs[j];
        }
        filtered[i] = sum;
    }

    // Process the end (partial convolution)
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

    // Check for unreasonably large signals
    if (signal.size() > 10000000) {  // 10M samples max
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

    // Calculate mean
    float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) / signal.size();

    // Subtract mean
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

        // Update peak with decay
        peakLevel = std::max(peakLevel * 0.9999f, absSample);

        // Calculate error
        float error = targetLevel - peakLevel;

        // Update AGC level
        if (error < 0) {
            // Signal too high - attack
            agcLevel *= (1.0f - agcAttackRate);
        } else {
            // Signal too low - decay
            agcLevel *= (1.0f + agcDecayRate);
        }

        // Limit AGC range
        agcLevel = clamp(agcLevel, 0.1f, 10.0f);

        // Apply AGC
        agcSignal[i] = signal[i] * agcLevel;
    }

    return agcSignal;
}

std::vector<float> PALBDemodulator::normalizeSignal(
    const std::vector<float>& signal)
{
    if (signal.empty()) return signal;

    // Find the sync tip (minimum) and peak white (maximum)
    auto minmax = std::minmax_element(signal.begin(), signal.end());
    float syncTip = *minmax.first;
    float peakWhite = *minmax.second;

    float range = peakWhite - syncTip;
    if (range < 0.001f) return signal;

    std::vector<float> normalized(signal.size());

    // Normalize with sync at 0.0 and white at 1.0
    // Black level should end up around 0.3
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
    const int minVSyncWidth = vsyncSamples * 0.5;  // Lower requirement

    int syncCount = 0;
    size_t bestSyncPos = 0;
    int maxSyncCount = 0;

    // Search through the signal
    for (size_t i = 0; i < signal.size(); i++) {
        if (signal[i] < SYNC_THRESHOLD) {
            syncCount++;

            if (syncCount > maxSyncCount) {
                maxSyncCount = syncCount;
                bestSyncPos = i - syncCount + 1;
            }
        } else {
            syncCount = 0;
        }

        // Check if we've found enough consecutive sync samples
        if (maxSyncCount >= minVSyncWidth) {
            syncStart = bestSyncPos;

            // Simple field detection
            fieldType = (bestSyncPos % (samplesPerLine * 2)) > samplesPerLine ? 1 : 0;

            lastVSyncPosition = bestSyncPos;
            qDebug() << "VSync detected at" << bestSyncPos << "width:" << maxSyncCount;
            return true;
        }
    }

    return false;
}

bool PALBDemodulator::detectHorizontalSync(
    const std::vector<float>& signal,
    size_t startPos,
    size_t& syncPos)
{
    const float SYNC_THRESHOLD = vSyncThreshold * 1.5f; // More lenient threshold
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
            // Check if we found a good enough sync
            if (syncCount >= hsyncSamples * 0.5) { // Lower requirement to 50%
                syncPos = i - syncCount;
                return true;
            }
            syncCount = 0;
        }
    }

    // Accept the best sync we found if it's reasonable
    if (maxSyncCount >= hsyncSamples * 0.3) { // Very lenient - 30% of expected
        syncPos = bestSyncPos;
        return true;
    }

    return false;
}

std::vector<float> PALBDemodulator::removeVBI(
    const std::vector<float>& signal)
{
    // Skip VBI lines at the beginning
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

    // If signal is too small, return as-is
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

    // If we can't find sync, just reshape the signal based on expected line length
    bool useFallbackMode = false;

    while (pos < signal.size() - samplesPerLine) {
        size_t syncPos;

        // Try to find horizontal sync
        bool syncFound = detectHorizontalSync(signal, pos, syncPos);

        if (syncFound) {
            syncFoundCount++;

            // Extract active video portion of the line
            size_t videoStart = syncPos + static_cast<size_t>(
                                    (PAL_H_SYNC_DURATION + PAL_BACK_PORCH) * effectiveSampleRate
                                    );

            if (videoStart + pixelsPerLine < signal.size()) {
                // Interpolate line to exact pixel count
                auto line = interpolateLine(signal, videoStart, pixelsPerLine);
                recovered.insert(recovered.end(), line.begin(), line.end());
                linesProcessed++;
            }

            // Move to next line with fractional accumulation
            pos = syncPos + samplesPerLine;
            fractionalPos += fractionalOffset;

            if (fractionalPos >= 1.0f) {
                pos += static_cast<size_t>(fractionalPos);
                fractionalPos -= static_cast<int>(fractionalPos);
            }
        } else {
            // No sync found - use fallback mode
            useFallbackMode = true;
            break;
        }

        // If we've processed enough lines or aren't finding syncs, switch to fallback
        if (linesProcessed > 10 && syncFoundCount < 5) {
            useFallbackMode = true;
            break;
        }
    }

    // FALLBACK MODE: No sync or poor sync - just reshape based on timing
    if (useFallbackMode || recovered.empty()) {
        recovered.clear();
        recovered.reserve(signal.size());

        // Skip the first portion to align better
        size_t skipSamples = static_cast<size_t>(horizontalOffset * samplesPerLine);
        pos = skipSamples;

        while (pos + pixelsPerLine < signal.size()) {
            // Just extract pixelsPerLine samples at regular intervals
            for (int i = 0; i < pixelsPerLine && pos + i < signal.size(); i++) {
                recovered.push_back(signal[pos + i]);
            }

            // Move to next line
            pos += samplesPerLine;

            // Add fractional offset
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

    // Calculate source samples available
    size_t sourceSamples = std::min(
        static_cast<size_t>(targetSamples),
        signal.size() - startPos
        );

    if (sourceSamples == static_cast<size_t>(targetSamples)) {
        // Direct copy if sizes match
        std::copy(signal.begin() + startPos,
                  signal.begin() + startPos + targetSamples,
                  line.begin());
    } else {
        // Linear interpolation
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
    // Simple bob deinterlacing - duplicate lines
    std::vector<float> deinterlaced;
    deinterlaced.reserve(signal.size() * 2);

    size_t numLines = signal.size() / pixelsPerLine;

    for (size_t line = 0; line < numLines; line++) {
        size_t start = line * pixelsPerLine;
        size_t end = std::min(start + pixelsPerLine, signal.size());

        // Add line twice for simple deinterlacing
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

    // Calculate actual available lines
    size_t availableLines = videoSignal.size() / pixelsPerLine;

    if (availableLines < 100) {  // Too few lines
        qDebug() << "ERROR: Too few lines available:" << availableLines;
        return QImage();
    }

    // Adjust height to available data
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

    // Create gamma lookup table
    std::vector<uint8_t> lut(256);
    float invGamma = 1.0f / gamma;

    for (int i = 0; i < 256; i++) {
        float normalized = i / 255.0f;
        float correctedVal = std::pow(normalized, invGamma);
        lut[i] = floatToUint8(correctedVal);
    }

    // Apply LUT to image
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
    // Shift to audio carrier (5.5 MHz offset from video)
    auto audioShifted = frequencyShift(signal, -AUDIO_CARRIER_OFFSET);

    // FM demodulate
    auto audio = fmDemodulateAtan2(audioShifted);

    // Lowpass filter to 15 kHz
    audio = lowPassFilter(audio, 15000.0f);

    // Decimate to 48 kHz if needed
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

    // Unwrap phase jumps
    while (delta > M_PI) delta -= 2.0f * M_PI;
    while (delta < -M_PI) delta += 2.0f * M_PI;

    return delta;
}
