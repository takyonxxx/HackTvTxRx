#include "palbdemodulator.h"
#include <cmath>
#include <algorithm>
#include <numeric>

PALBDemodulator::PALBDemodulator(double _sampleRate, QObject *parent)
    : QObject(parent)
    , sampleRate(_sampleRate)
{
    m_fltBufferI.fill(0.0f);
    m_fltBufferQ.fill(0.0f);

    qDebug() << "PAL-B Demodulator initialized:";
    qDebug() << "  Sample Rate:" << sampleRate / 1e6 << "MHz";
    qDebug() << "  Video Carrier:" << videoCarrier / 1e6 << "MHz";
    qDebug() << "  Audio Carrier:" << AUDIO_CARRIER / 1e6 << "MHz";
    qDebug() << "  Color Subcarrier:" << COLOR_SUBCARRIER / 1e6 << "MHz";
}

// ============================================================================
// MAIN DEMODULATION FUNCTION
// ============================================================================
PALBDemodulator::DemodulatedFrame PALBDemodulator::demodulate(
    const std::vector<std::complex<float>>& samples)
{
    DemodulatedFrame frame;

    try {
        if (samples.empty()) {
            qWarning() << "Empty samples received";
            return frame;
        }

        // ====================================================================
        // VIDEO PROCESSING CHAIN
        // ====================================================================

        // 1. Frequency shift to baseband
        auto shiftedVideo = frequencyShift(
            std::vector<std::complex<float>>(samples),
            videoCarrier
            );

        // 2. Low-pass filter (video bandwidth)
        float videoBandwidth = 5.5e6f;
        shiftedVideo = complexLowPassFilter(std::move(shiftedVideo), videoBandwidth);

        // 3. ÖNCE decimatedComplex'i al (move etmeden önce!)
        auto decimatedComplex = decimateComplex(
            std::vector<std::complex<float>>(shiftedVideo),  // KOPYA AL
            decimationFactor
            );

        // 4. AM demodulation (artık shiftedVideo'yu move edebiliriz)
        auto videoSignal = amDemodulate(std::move(shiftedVideo));

        // 5. Decimate
        videoSignal = decimate(std::move(videoSignal), decimationFactor);

        double workingSampleRate = sampleRate / decimationFactor;
        double savedRate = sampleRate;
        sampleRate = workingSampleRate;

        // 6. Remove DC offset
        videoSignal = removeDCOffset(std::move(videoSignal));

        // 7. Fix polarity if needed
        if (!videoSignal.empty()) {
            float sum = std::accumulate(videoSignal.begin(), videoSignal.end(), 0.0f);
            float mean = sum / videoSignal.size();

            if (mean < -0.1f) {
                qDebug() << "Inverting polarity";
                for (auto& sample : videoSignal) {
                    sample = -sample;
                }
            }
        }

        // 8. Apply AGC
        videoSignal = applyAGC(std::move(videoSignal));

        // 9. Detect and align to vertical sync
        size_t syncStart = 0;
        bool syncFound = detectVerticalSync(videoSignal, syncStart);

        if (syncFound) {
            videoSignal.erase(videoSignal.begin(), videoSignal.begin() + syncStart);

            if (syncStart < decimatedComplex.size()) {
                decimatedComplex.erase(
                    decimatedComplex.begin(),
                    decimatedComplex.begin() + syncStart
                    );
            }
        }

        // 10. Remove VBI (Vertical Blanking Interval)
        videoSignal = removeVBI(std::move(videoSignal));
        decimatedComplex = removeVBIComplex(std::move(decimatedComplex));

        // 11. Timing recovery (line synchronization)
        videoSignal = timingRecovery(std::move(videoSignal));
        decimatedComplex = timingRecoveryComplex(std::move(decimatedComplex));

        // 12. Field extraction (KAPATILDI - Test için)
        if (enableDeinterlace) {
            videoSignal = extractSingleField(std::move(videoSignal), true);  // Odd field
        }

        sampleRate = savedRate;

        // 13. Extract chroma information
        auto chromaUV = extractChroma(decimatedComplex, videoSignal.size());
        // 14. Convert YUV to RGB image
        frame.image = convertYUVtoRGB(videoSignal, chromaUV.first, chromaUV.second);

        if (frame.image.isNull()) {
            qWarning() << "Image is NULL!";
        }

        // ====================================================================
        // AUDIO PROCESSING CHAIN
        // ====================================================================

        // 1. Shift audio carrier to baseband
        auto shiftedAudio = frequencyShift(
            std::vector<std::complex<float>>(samples),
            AUDIO_CARRIER  // 5.5 MHz
            );

        // 2. Low-pass filter for audio bandwidth (±200 kHz)
        shiftedAudio = complexLowPassFilter(std::move(shiftedAudio), 200e3f);

        // 3. ÖNCE quadrature rate'e decimate (240 kHz - 48'in katı)
        int audioDecimation1 = static_cast<int>(savedRate / 240e3);  // 20MHz / 240kHz ≈ 83
        auto decimatedAudio = decimateComplex(std::move(shiftedAudio), audioDecimation1);
        double audioQuadratureRate = savedRate / audioDecimation1;

        // 4. FM Demodulation
        frame.audio = fmDemodulateAudio(decimatedAudio, audioQuadratureRate);

        // 5. Remove DC offset
        if (!frame.audio.empty()) {
            float sum = std::accumulate(frame.audio.begin(), frame.audio.end(), 0.0f);
            float dcOffset = sum / frame.audio.size();
            for (auto& s : frame.audio) {
                s -= dcOffset;
            }
        }

        // 6. Apply low-pass filter
        frame.audio = applyAudioLowPassFilter(std::move(frame.audio));

        // 7. RESAMPLE 240 kHz → 48 kHz (decimation: 5)
        int audioDecimation2 = 5;  // 240 kHz / 5 = 48 kHz
        std::vector<float> resampledAudio;
        resampledAudio.reserve(frame.audio.size() / audioDecimation2);

        for (size_t i = 0; i < frame.audio.size(); i += audioDecimation2) {
            resampledAudio.push_back(frame.audio[i]);
        }

        frame.audio = std::move(resampledAudio);

        // 8. Normalize
        if (!frame.audio.empty()) {
            float maxAudio = 0.0f;
            for (const auto& s : frame.audio) {
                maxAudio = std::max(maxAudio, std::abs(s));
            }
            if (maxAudio > 1e-6f) {
                float gain = 0.8f / maxAudio;
                for (auto& s : frame.audio) {
                    s = std::clamp(s * gain, -1.0f, 1.0f);
                }
            }
        }
    }
    catch (const std::bad_alloc& e) {
        qCritical() << "OUT OF MEMORY in PAL demodulation:" << e.what();
        frame.image = QImage(pixelsPerLine, visibleLines, QImage::Format_RGB888);
        frame.image.fill(Qt::black);
    }
    catch (const std::exception& e) {
        qCritical() << "EXCEPTION in PAL demodulation:" << e.what();
        frame.image = QImage(pixelsPerLine, visibleLines, QImage::Format_RGB888);
        frame.image.fill(Qt::black);
    }

    return frame;
}

std::vector<float> PALBDemodulator::demodulateAudioOnly(
    const std::vector<std::complex<float>>& samples)
{
    std::vector<float> audio;

    try {
        if (samples.empty()) return audio;

        // Sadece audio processing - video atla
        auto shiftedAudio = frequencyShift(
            std::vector<std::complex<float>>(samples),
            AUDIO_CARRIER
            );

        shiftedAudio = complexLowPassFilter(std::move(shiftedAudio), 200e3f);

        int audioDecimation = static_cast<int>(sampleRate / 240e3);
        auto decimatedAudio = decimateComplex(std::move(shiftedAudio), audioDecimation);
        double audioQuadratureRate = sampleRate / audioDecimation;

        audio = fmDemodulateAudio(decimatedAudio, audioQuadratureRate);

        // DC offset removal
        if (!audio.empty()) {
            float sum = std::accumulate(audio.begin(), audio.end(), 0.0f);
            float dcOffset = sum / audio.size();
            for (auto& s : audio) s -= dcOffset;
        }

        // Low-pass filter
        audio = applyAudioLowPassFilter(std::move(audio));

        // Resample to 48 kHz
        int resampleFactor = static_cast<int>(audioQuadratureRate / 48000.0);
        std::vector<float> resampledAudio;
        resampledAudio.reserve(audio.size() / resampleFactor);

        for (size_t i = 0; i < audio.size(); i += resampleFactor) {
            resampledAudio.push_back(audio[i]);
        }

        audio = std::move(resampledAudio);

        // Normalize
        if (!audio.empty()) {
            float maxAudio = 0.0f;
            for (const auto& s : audio) {
                maxAudio = std::max(maxAudio, std::abs(s));
            }
            if (maxAudio > 1e-6f) {
                float gain = 0.8f / maxAudio;
                for (auto& s : audio) {
                    s = std::clamp(s * gain, -1.0f, 1.0f);
                }
            }
        }

    } catch (const std::exception& e) {
        qCritical() << "Audio-only demodulation error:" << e.what();
    }

    return audio;
}

QImage PALBDemodulator::demodulateVideoOnly(
    const std::vector<std::complex<float>>& samples)
{
    QImage image;

    try {
        if (samples.empty()) return image;

        // Sadece video processing - audio atla
        auto shiftedVideo = frequencyShift(
            std::vector<std::complex<float>>(samples),
            videoCarrier
            );

        float videoBandwidth = 5.5e6f;
        shiftedVideo = complexLowPassFilter(std::move(shiftedVideo), videoBandwidth);

        auto decimatedComplex = decimateComplex(
            std::vector<std::complex<float>>(shiftedVideo),
            decimationFactor
            );

        auto videoSignal = amDemodulate(std::move(shiftedVideo));
        videoSignal = decimate(std::move(videoSignal), decimationFactor);

        double workingSampleRate = sampleRate / decimationFactor;
        double savedRate = sampleRate;
        sampleRate = workingSampleRate;

        videoSignal = removeDCOffset(std::move(videoSignal));

        if (!videoSignal.empty()) {
            float sum = std::accumulate(videoSignal.begin(), videoSignal.end(), 0.0f);
            float mean = sum / videoSignal.size();
            if (mean < -0.1f) {
                for (auto& sample : videoSignal) sample = -sample;
            }
        }

        videoSignal = applyAGC(std::move(videoSignal));

        size_t syncStart = 0;
        if (detectVerticalSync(videoSignal, syncStart)) {
            videoSignal.erase(videoSignal.begin(), videoSignal.begin() + syncStart);
            if (syncStart < decimatedComplex.size()) {
                decimatedComplex.erase(decimatedComplex.begin(),
                                       decimatedComplex.begin() + syncStart);
            }
        }

        videoSignal = removeVBI(std::move(videoSignal));
        decimatedComplex = removeVBIComplex(std::move(decimatedComplex));

        videoSignal = timingRecovery(std::move(videoSignal));
        decimatedComplex = timingRecoveryComplex(std::move(decimatedComplex));

        if (enableDeinterlace) {
            videoSignal = deinterlaceFields(std::move(videoSignal));
        }

        sampleRate = savedRate;

        auto chromaUV = extractChroma(decimatedComplex, videoSignal.size());
        image = convertYUVtoRGB(videoSignal, chromaUV.first, chromaUV.second);

    } catch (const std::exception& e) {
        qCritical() << "Video-only demodulation error:" << e.what();
        image = QImage(pixelsPerLine, visibleLines, QImage::Format_RGB888);
        image.fill(Qt::black);
    }

    return image;
}

// ============================================================================
// FREQUENCY OPERATIONS
// ============================================================================
std::vector<std::complex<float>> PALBDemodulator::frequencyShift(
    std::vector<std::complex<float>> signal,
    double shiftFreq)
{
    if (signal.empty()) return signal;

    double phaseIncrement = -2.0 * M_PI * shiftFreq / sampleRate;
    std::complex<float> phase(1.0f, 0.0f);
    std::complex<float> phaseStep(
        std::cos(phaseIncrement),
        std::sin(phaseIncrement)
        );

    for (size_t i = 0; i < signal.size(); ++i) {
        signal[i] *= phase;
        phase *= phaseStep;

        // Periodic normalization to prevent accumulation error
        if (i % 10000 == 0) {
            float mag = std::abs(phase);
            if (mag > 0.0f) {
                phase /= mag;
            }
        }
    }

    return signal;
}

// ============================================================================
// DEMODULATION
// ============================================================================
std::vector<float> PALBDemodulator::amDemodulate(
    const std::vector<std::complex<float>>& signal)
{
    std::vector<float> demodulated;
    demodulated.reserve(signal.size());

    for (const auto& sample : signal) {
        demodulated.push_back(std::abs(sample));
    }

    return demodulated;
}

std::vector<float> PALBDemodulator::fmDemodulateYDiff(
    const std::vector<std::complex<float>>& signal)
{
    if (signal.empty()) return std::vector<float>();

    std::vector<float> demodulated(signal.size(), 0.0f);
    float prevPhase = std::arg(signal[0]);

    for (size_t i = 1; i < signal.size(); ++i) {
        float phase = std::arg(signal[i]);
        float diff = phase - prevPhase;

        // Unwrap phase
        if (diff > M_PI) diff -= 2.0f * M_PI;
        if (diff < -M_PI) diff += 2.0f * M_PI;

        demodulated[i] = diff;
        prevPhase = phase;
    }

    // Normalize
    float maxVal = 0.0f;
    for (auto s : demodulated) {
        maxVal = std::max(maxVal, std::abs(s));
    }

    if (maxVal > 1e-6f) {
        for (auto& s : demodulated) {
            s /= maxVal;
        }
    }

    return demodulated;
}

// ============================================================================
// FILTERING
// ============================================================================
std::vector<float> PALBDemodulator::designLowPassFIR(
    int numTaps,
    float cutoffFreq,
    float sampleRate)
{
    if (numTaps <= 0 || numTaps > 1000) {
        qWarning() << "Invalid numTaps:" << numTaps;
        return std::vector<float>();
    }

    if (cutoffFreq <= 0 || cutoffFreq >= sampleRate / 2) {
        qWarning() << "Invalid cutoff frequency:" << cutoffFreq;
        return std::vector<float>();
    }

    std::vector<float> h(numTaps, 0.0f);
    float fc = cutoffFreq / sampleRate;
    int center = numTaps / 2;

    // Generate sinc function with Hamming window
    for (int i = 0; i < numTaps; ++i) {
        int n = i - center;

        if (n == 0) {
            h[i] = 2.0f * fc;
        } else {
            h[i] = std::sin(2.0f * M_PI * fc * n) / (M_PI * n);
        }

        // Hamming window
        float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (numTaps - 1));
        h[i] *= window;
    }

    // Normalize
    float sum = std::accumulate(h.begin(), h.end(), 0.0f);
    if (sum != 0.0f && !std::isnan(sum) && !std::isinf(sum)) {
        for (auto& coef : h) {
            coef /= sum;
        }
    }

    return h;
}

std::vector<std::complex<float>> PALBDemodulator::complexLowPassFilter(
    std::vector<std::complex<float>> signal,
    float cutoffFreq)
{
    if (signal.empty()) return signal;

    const int numTaps = 51;
    auto h = designLowPassFIR(numTaps, cutoffFreq, sampleRate);

    if (h.empty()) return signal;

    int center = numTaps / 2;

    // Zero-pad signal
    std::vector<std::complex<float>> padded(signal.size() + numTaps, {0.0f, 0.0f});
    std::copy(signal.begin(), signal.end(), padded.begin() + center);

    // Apply filter (in-place to save memory)
    for (size_t i = 0; i < signal.size(); ++i) {
        std::complex<float> sum(0.0f, 0.0f);
        for (int j = 0; j < numTaps; ++j) {
            sum += padded[i + j] * h[j];
        }
        signal[i] = sum;
    }

    return signal;
}

std::vector<float> PALBDemodulator::lowPassFilter(
    std::vector<float> signal,
    float cutoffFreq)
{
    if (signal.empty()) return signal;

    const int numTaps = 51;
    auto h = designLowPassFIR(numTaps, cutoffFreq, sampleRate);

    if (h.empty()) return signal;

    int center = numTaps / 2;
    size_t validStart = center;
    size_t validEnd = signal.size() > center ? signal.size() - center : 0;

    if (validEnd <= validStart) return signal;

    std::vector<float> filtered = signal;

    for (size_t i = validStart; i < validEnd; ++i) {
        float sum = 0.0f;
        for (int j = 0; j < numTaps; ++j) {
            size_t idx = i - center + j;
            if (idx < signal.size()) {
                sum += signal[idx] * h[j];
            }
        }
        filtered[i] = sum;
    }

    return filtered;
}

// ============================================================================
// DECIMATION
// ============================================================================
std::vector<float> PALBDemodulator::decimate(
    std::vector<float> signal,
    int factor)
{
    if (factor <= 1 || signal.empty()) return signal;

    std::vector<float> decimated;
    decimated.reserve(signal.size() / factor + 1);

    for (size_t i = 0; i < signal.size(); i += factor) {
        decimated.push_back(signal[i]);
    }

    return decimated;
}

std::vector<std::complex<float>> PALBDemodulator::decimateComplex(
    std::vector<std::complex<float>> signal,
    int factor)
{
    if (factor <= 1 || signal.empty()) return signal;

    std::vector<std::complex<float>> decimated;
    decimated.reserve(signal.size() / factor + 1);

    for (size_t i = 0; i < signal.size(); i += factor) {
        decimated.push_back(signal[i]);
    }

    return decimated;
}

// ============================================================================
// SIGNAL CONDITIONING
// ============================================================================
std::vector<float> PALBDemodulator::removeDCOffset(
    std::vector<float> signal)
{
    if (signal.empty()) return signal;

    float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) / signal.size();

    for (auto& sample : signal) {
        sample -= mean;
    }

    return signal;
}

std::vector<float> PALBDemodulator::applyAGC(
    std::vector<float> signal)
{
    if (signal.empty()) return signal;

    // Calculate RMS for proper normalization
    float rms = 0.0f;
    for (const auto& sample : signal) {
        rms += sample * sample;
    }
    rms = std::sqrt(rms / signal.size());

    if (rms < 1e-6f) {
        qWarning() << "Signal RMS too low, skipping AGC";
        return signal;
    }

    // Normalize to target RMS
    const float targetRMS = 0.5f;
    float globalGain = targetRMS / rms;
    globalGain = std::clamp(globalGain, 0.1f, 100.0f);

    // Apply global normalization first
    for (auto& sample : signal) {
        sample *= globalGain;
    }

    // Then apply adaptive AGC
    const float targetAmplitude = 0.7f;
    float currentGain = 1.0f;

    for (auto& sample : signal) {
        float absValue = std::abs(sample);
        float desiredGain = targetAmplitude / std::max(absValue, 1e-6f);

        if (currentGain > desiredGain) {
            currentGain += agcAttackRate * (desiredGain - currentGain);
        } else {
            currentGain += agcDecayRate * (desiredGain - currentGain);
        }

        currentGain = std::clamp(currentGain, 0.1f, 10.0f);
        sample *= currentGain;
    }

    // Calculate final RMS
    rms = 0.0f;
    for (const auto& sample : signal) {
        rms += sample * sample;
    }
    rms = std::sqrt(rms / signal.size());

    return signal;
}

// ============================================================================
// SYNCHRONIZATION
// ============================================================================
bool PALBDemodulator::detectVerticalSync(
    const std::vector<float>& signal,
    size_t& syncStart)
{
    if (signal.size() < 10000) {
        qWarning() << "Signal too short for sync detection:" << signal.size();
        return false;
    }

    // Calculate signal statistics
    float minVal = *std::min_element(signal.begin(), signal.end());
    float maxVal = *std::max_element(signal.begin(), signal.end());
    float range = maxVal - minVal;

    if (range < 0.01f) {
        qWarning() << "Signal range too small:" << range;
        return false;
    }

    // Adaptive threshold - sync pulses are at the minimum
    float syncThreshold = minVal + range * 0.3f;  // 30% from minimum 

    const size_t minSyncDuration = static_cast<size_t>(sampleRate * 27e-6);   // 27 μs
    const size_t maxSyncDuration = static_cast<size_t>(sampleRate * 200e-6);  // 200 μs

    // Look for long sync pulse (vertical sync = multiple H-sync pulses)
    size_t longestSyncPos = 0;
    size_t longestSyncDuration = 0;

    for (size_t i = 0; i < signal.size() - maxSyncDuration; ++i) {
        if (signal[i] < syncThreshold) {
            size_t lowSamples = 0;

            // Count consecutive samples below threshold
            while (i + lowSamples < signal.size() &&
                   signal[i + lowSamples] < syncThreshold &&
                   lowSamples < maxSyncDuration * 2) {
                ++lowSamples;
            }

            // Track longest sync pulse (vertical sync is longer than H-sync)
            if (lowSamples > longestSyncDuration && lowSamples >= minSyncDuration) {
                longestSyncDuration = lowSamples;
                longestSyncPos = i;
            }

            i += lowSamples;
        }
    }

    // Need at least 3 H-sync pulses worth of sync (vertical sync indicator)
    const size_t minVerticalSyncDuration = static_cast<size_t>(sampleRate * 100e-6); // 100 μs

    if (longestSyncDuration >= minVerticalSyncDuration) {
        syncStart = longestSyncPos;
        return true;
    }
    syncStart = 0;
    return false;
}

std::vector<float> PALBDemodulator::removeVBI(
    std::vector<float> signal)
{
    const double samplesPerLine = sampleRate * lineDuration;
    const size_t vbiSamples = static_cast<size_t>(vbiLines * samplesPerLine);

    if (signal.size() <= vbiSamples) return signal;

    signal.erase(signal.begin(), signal.begin() + vbiSamples);
    return signal;
}

std::vector<std::complex<float>> PALBDemodulator::removeVBIComplex(
    std::vector<std::complex<float>> signal)
{
    const double samplesPerLine = sampleRate * lineDuration;
    const size_t vbiSamples = static_cast<size_t>(vbiLines * samplesPerLine);

    if (signal.size() <= vbiSamples) return signal;

    signal.erase(signal.begin(), signal.begin() + vbiSamples);
    return signal;
}

// ============================================================================
// TIMING RECOVERY
// ============================================================================
std::vector<float> PALBDemodulator::timingRecovery(
    std::vector<float> signal)
{
    if (signal.empty()) return signal;

    const double samplesPerLine = sampleRate * lineDuration;
    const size_t numLines = static_cast<size_t>(signal.size() / samplesPerLine);

    if (numLines == 0) return signal;

    std::vector<float> recovered;
    recovered.reserve(numLines * pixelsPerLine);

    const size_t skipSamples = static_cast<size_t>(samplesPerLine * horizontalOffset);
    const double activeSamplesPerLine = samplesPerLine - skipSamples;

    for (size_t line = 0; line < numLines; ++line) {
        size_t lineStart = static_cast<size_t>(line * samplesPerLine) + skipSamples;

        if (lineStart >= signal.size()) break;

        for (int pixel = 0; pixel < pixelsPerLine; ++pixel) {
            double srcPos = lineStart + (pixel * activeSamplesPerLine / pixelsPerLine);
            size_t idx = static_cast<size_t>(srcPos);

            if (idx + 1 >= signal.size()) {
                if (idx < signal.size()) {
                    recovered.push_back(signal[idx]);
                }
                break;
            }

            // Linear interpolation
            double frac = srcPos - idx;
            float interpolated = signal[idx] * (1.0f - frac) + signal[idx + 1] * frac;
            recovered.push_back(interpolated);
        }
    }

    return recovered;
}

std::vector<std::complex<float>> PALBDemodulator::timingRecoveryComplex(
    std::vector<std::complex<float>> signal)
{
    if (signal.empty()) return signal;

    const double samplesPerLine = sampleRate * lineDuration;
    const size_t numLines = static_cast<size_t>(signal.size() / samplesPerLine);

    if (numLines == 0) return signal;

    std::vector<std::complex<float>> recovered;
    recovered.reserve(numLines * pixelsPerLine);

    const size_t skipSamples = static_cast<size_t>(samplesPerLine * horizontalOffset);
    const double activeSamplesPerLine = samplesPerLine - skipSamples;

    for (size_t line = 0; line < numLines; ++line) {
        size_t lineStart = static_cast<size_t>(line * samplesPerLine) + skipSamples;

        if (lineStart >= signal.size()) break;

        for (int pixel = 0; pixel < pixelsPerLine; ++pixel) {
            double srcPos = lineStart + (pixel * activeSamplesPerLine / pixelsPerLine);
            size_t idx = static_cast<size_t>(srcPos);

            if (idx + 1 >= signal.size()) {
                if (idx < signal.size()) {
                    recovered.push_back(signal[idx]);
                }
                break;
            }

            // Linear interpolation
            double frac = srcPos - idx;
            std::complex<float> interpolated =
                signal[idx] * static_cast<float>(1.0 - frac) +
                signal[idx + 1] * static_cast<float>(frac);
            recovered.push_back(interpolated);
        }
    }

    return recovered;
}

// ============================================================================
// FIELD PROCESSING
// ============================================================================
std::vector<float> PALBDemodulator::extractSingleField(
    std::vector<float> signal,
    bool oddField)
{
    if (signal.size() < pixelsPerLine * 2) {
        qWarning() << "Signal too small for field extraction:" << signal.size();
        return signal;
    }

    std::vector<float> field;
    size_t totalLines = signal.size() / pixelsPerLine;

    field.reserve(totalLines / 2 * pixelsPerLine);

    // Extract odd or even lines
    for (size_t line = oddField ? 0 : 1; line < totalLines; line += 2) {
        size_t lineStart = line * pixelsPerLine;
        size_t lineEnd = lineStart + pixelsPerLine;

        if (lineEnd <= signal.size()) {
            field.insert(field.end(),
                         signal.begin() + lineStart,
                         signal.begin() + lineEnd);
        }
    }

    return field;
}

std::vector<float> PALBDemodulator::deinterlaceFields(
    std::vector<float> signal)
{
    // Extract only odd field (first field)
    return extractSingleField(std::move(signal), true);
}

// ============================================================================
// CHROMA EXTRACTION
// ============================================================================
std::pair<std::vector<float>, std::vector<float>> PALBDemodulator::extractChroma(
    const std::vector<std::complex<float>>& signal,
    size_t targetSize)
{
    std::vector<float> chromaU;
    std::vector<float> chromaV;

    if (signal.empty()) {
        chromaU.resize(targetSize, 0.0f);
        chromaV.resize(targetSize, 0.0f);
        return {chromaU, chromaV};
    }

    size_t actualSize = std::min(signal.size(), targetSize);
    chromaU.reserve(actualSize);
    chromaV.reserve(actualSize);

    double chromaFreq = COLOR_SUBCARRIER / 2.0;
    double phaseInc = 2.0 * M_PI * chromaFreq / (sampleRate / 2.0);

    bool vSwitch = false;
    double samplesPerLine = (sampleRate / 2.0) * lineDuration;
    int samplesPerLineInt = static_cast<int>(samplesPerLine);

    for (size_t i = 0; i < actualSize; ++i) {
        float phase = static_cast<float>(i * phaseInc);

        // Demodulate U and V
        float u = signal[i].real() * std::cos(phase) + signal[i].imag() * std::sin(phase);
        float v = -signal[i].real() * std::sin(phase) + signal[i].imag() * std::cos(phase);

        // PAL phase alternation (V inverts every line)
        if (samplesPerLineInt > 0 && i > 0 && i % samplesPerLineInt == 0) {
            vSwitch = !vSwitch;
        }

        if (vSwitch) {
            v = -v;
        }

        chromaU.push_back(u * 2.0f);
        chromaV.push_back(v * 2.0f);
    }

    // Pad if needed
    while (chromaU.size() < targetSize) {
        chromaU.push_back(0.0f);
        chromaV.push_back(0.0f);
    }

    return {chromaU, chromaV};
}

// ============================================================================
// IMAGE CONVERSION
// ============================================================================
QImage PALBDemodulator::convertToImage(
    const std::vector<float>& videoSignal,
    float brightness,
    float contrast)
{
    QImage image(pixelsPerLine, visibleLines, QImage::Format_Grayscale8);

    size_t expectedSize = pixelsPerLine * visibleLines;

    if (videoSignal.size() < expectedSize) {
        image.fill(Qt::black);
        return image;
    }

    float minVal = *std::min_element(
        videoSignal.begin(),
        videoSignal.begin() + expectedSize
        );
    float maxVal = *std::max_element(
        videoSignal.begin(),
        videoSignal.begin() + expectedSize
        );

    if (maxVal == minVal) {
        maxVal = minVal + 1.0f;
    }

    for (int line = 0; line < visibleLines; ++line) {
        uchar* scanLine = image.scanLine(line);

        for (int pixel = 0; pixel < pixelsPerLine; ++pixel) {
            size_t index = line * pixelsPerLine + pixel;

            if (index < videoSignal.size()) {
                float normalized = (videoSignal[index] - minVal) / (maxVal - minVal);
                normalized = (normalized - 0.5f) * contrast + 0.5f + brightness;

                int value = static_cast<int>(normalized * 255.0f);
                value = std::clamp(value, 0, 255);

                scanLine[pixel] = static_cast<uchar>(value);
            } else {
                scanLine[pixel] = 0;
            }
        }
    }

    return image;
}

QImage PALBDemodulator::convertYUVtoRGB(
    const std::vector<float>& luma,
    const std::vector<float>& chromaU,
    const std::vector<float>& chromaV)
{
    QImage image(pixelsPerLine, visibleLines, QImage::Format_RGB888);

    size_t expectedSize = pixelsPerLine * visibleLines;

    if (luma.size() < expectedSize) {
        image.fill(Qt::black);
        return image;
    }

    // Normalize luma
    float minY = *std::min_element(luma.begin(), luma.begin() + expectedSize);
    float maxY = *std::max_element(luma.begin(), luma.begin() + expectedSize);

    if (maxY == minY) {
        maxY = minY + 1.0f;
    }

    for (int line = 0; line < visibleLines; ++line) {
        uchar* scanLine = image.scanLine(line);

        for (int pixel = 0; pixel < pixelsPerLine; ++pixel) {
            size_t index = line * pixelsPerLine + pixel;

            if (index < luma.size()) {
                // Normalize Y
                float y = (luma[index] - minY) / (maxY - minY);

                // Get U and V
                float u = (index < chromaU.size()) ? chromaU[index] * 0.5f : 0.0f;
                float v = (index < chromaV.size()) ? chromaV[index] * 0.5f : 0.0f;

                // YUV to RGB conversion (ITU-R BT.601)
                float r = y + 1.140f * v;
                float g = y - 0.395f * u - 0.581f * v;
                float b = y + 2.032f * u;

                // Clamp and convert to 8-bit
                int red = std::clamp(static_cast<int>(r * 255.0f), 0, 255);
                int green = std::clamp(static_cast<int>(g * 255.0f), 0, 255);
                int blue = std::clamp(static_cast<int>(b * 255.0f), 0, 255);

                scanLine[pixel * 3 + 0] = static_cast<uchar>(red);
                scanLine[pixel * 3 + 1] = static_cast<uchar>(green);
                scanLine[pixel * 3 + 2] = static_cast<uchar>(blue);
            } else {
                scanLine[pixel * 3 + 0] = 0;
                scanLine[pixel * 3 + 1] = 0;
                scanLine[pixel * 3 + 2] = 0;
            }
        }
    }

    return image;
}

// CPP'ye ekle (palbdemodulator.cpp):

std::vector<float> PALBDemodulator::fmDemodulateAudio(
    const std::vector<std::complex<float>>& samples,
    double quadratureRate)
{
    if (samples.size() < 2) {
        return std::vector<float>();
    }

    std::vector<float> demodulated;
    demodulated.reserve(samples.size());

    std::complex<float> prevSample = samples[0];
    demodulated.push_back(0.0f);  // İlk sample

    // Phase discriminator (FM radyo gibi)
    for (size_t i = 1; i < samples.size(); i++) {
        // Conjugate multiplication
        std::complex<float> product = samples[i] * std::conj(prevSample);
        float phase = std::arg(product);

        // NaN/Inf kontrolü
        if (std::isnan(phase) || std::isinf(phase)) {
            phase = 0.0f;
        }

        // Normalize by max deviation (50 kHz for PAL-B audio)
        // phase * (sample_rate / (2*π * max_deviation))
        float demodValue = phase * (quadratureRate / (2.0f * M_PI * 50e3f));

        // Soft clipping
        demodValue = softClip(demodValue);

        demodulated.push_back(demodValue);
        prevSample = samples[i];
    }

    return demodulated;
}

std::vector<float> PALBDemodulator::applyAudioLowPassFilter(
    std::vector<float> signal)
{
    if (signal.empty()) return signal;

    // Simple IIR low-pass filter (similar to FM demodulator)
    const float alpha = 0.3f;  // Adjust for cutoff (~15 kHz)

    float prev = signal[0];
    for (size_t i = 1; i < signal.size(); ++i) {
        signal[i] = prev + alpha * (signal[i] - prev);
        prev = signal[i];
    }

    return signal;
}

float PALBDemodulator::softClip(float x)
{
    const float threshold = 0.8f;
    if (x > threshold) {
        return threshold + std::tanh(x - threshold);
    } else if (x < -threshold) {
        return -threshold - std::tanh(-x - threshold);
    }
    return x;
}
