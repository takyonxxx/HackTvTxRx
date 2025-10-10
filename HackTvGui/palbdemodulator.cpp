#include "palbdemodulator.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <QMutexLocker>
#include <QThread>

PALBDemodulator::PALBDemodulator(double _sampleRate, QObject *parent)
    : QObject(parent), sampleRate(_sampleRate)
{
    m_fltBufferI.fill(0.0f);
    m_fltBufferQ.fill(0.0f);
}

PALBDemodulator::DemodulatedFrame PALBDemodulator::demodulate(
    const std::vector<std::complex<float>>& samples)
{
    DemodulatedFrame frame;

    try {
        qDebug() << "=== Starting PAL Demodulation (Thread:" << QThread::currentThreadId() << ") ===";
        qDebug() << "Sample count:" << samples.size();
        qDebug() << "Sample rate:" << sampleRate;

        if (samples.empty()) {
            qWarning() << "Empty samples!";
            return frame;
        }

        // Check if we have enough samples for at least one frame
        double frameTime = 0.04; // 40ms for full PAL frame (2 fields)
        size_t minSamplesNeeded = static_cast<size_t>(sampleRate * frameTime);
        qDebug() << "Minimum samples needed for one frame:" << minSamplesNeeded;

        if (samples.size() < minSamplesNeeded) {
            qWarning() << "WARNING: Not enough samples! Have:" << samples.size()
                       << "Need at least:" << minSamplesNeeded;
        }

        // Step 1: Frequency shift to isolate the video carrier
        qDebug() << "Step 1: Frequency shift...";
        auto shiftedVideo = frequencyShift(samples, VIDEO_CARRIER);
        qDebug() << "  Result size:" << shiftedVideo.size();

        // Step 2: Apply complex low-pass filter to remove unwanted frequencies
        qDebug() << "Step 2: Complex low-pass filter...";
        float videoBandwidth = 5.5e6;
        shiftedVideo = complexLowPassFilter(shiftedVideo, videoBandwidth);
        qDebug() << "  Result size:" << shiftedVideo.size();

        // Step 3: AM demodulate (envelope detection)
        qDebug() << "Step 3: AM demodulation...";
        auto videoSignal = amDemodulate(shiftedVideo);
        qDebug() << "  Result size:" << videoSignal.size();

        // Step 4: Decimate to reduce sample rate
        qDebug() << "Step 4: Decimation...";
        int decimationFactor = 2;
        videoSignal = decimate(videoSignal, decimationFactor);
        double workingSampleRate = sampleRate / decimationFactor;
        qDebug() << "  Result size:" << videoSignal.size();
        qDebug() << "  Working sample rate:" << workingSampleRate;

        // Step 5: Remove DC offset
        qDebug() << "Step 5: Remove DC offset...";
        videoSignal = removeDCOffset(videoSignal);

        // Check signal statistics
        if (!videoSignal.empty()) {
            float minVal = *std::min_element(videoSignal.begin(), videoSignal.end());
            float maxVal = *std::max_element(videoSignal.begin(), videoSignal.end());
            qDebug() << "  Signal range: min=" << minVal << "max=" << maxVal;

            // Check if signal might be inverted
            float sum = std::accumulate(videoSignal.begin(), videoSignal.end(), 0.0f);
            float mean = sum / videoSignal.size();
            qDebug() << "  Signal mean:" << mean;

            if (mean < -0.1f) {
                qDebug() << "  Signal appears inverted, flipping...";
                for (auto& sample : videoSignal) {
                    sample = -sample;
                }
            }
        }

        // Step 6: Apply AGC
        qDebug() << "Step 6: AGC...";
        videoSignal = applyAGC(videoSignal);

        // Step 7: Vertical sync detection and alignment
        qDebug() << "Step 7: Sync detection...";
        size_t syncStart = 0;
        double savedRate = sampleRate;
        sampleRate = workingSampleRate;

        if (detectVerticalSync(videoSignal, syncStart)) {
            qDebug() << "  Sync detected at:" << syncStart;
            std::vector<float> syncedSignal(videoSignal.begin() + syncStart, videoSignal.end());
            videoSignal = syncedSignal;
        } else {
            qDebug() << "  Warning: Sync not detected, using full signal";
        }

        // Step 8: Remove Vertical Blanking Interval
        qDebug() << "Step 8: Remove VBI...";
        videoSignal = removeVBI(videoSignal);
        qDebug() << "  Result size:" << videoSignal.size();

        // Step 9: Timing recovery - resample to correct pixel rate
        qDebug() << "Step 9: Timing recovery...";
        videoSignal = timingRecovery(videoSignal);
        sampleRate = savedRate; // Restore original
        qDebug() << "  Result size:" << videoSignal.size();

        // Step 10: Apply final low-pass filter (DEVRE DIŞI - zaten Step 2'de filtrelendi)
        qDebug() << "Step 10: Final low-pass filter...";

        // Filtreyi devre dışı bırak - zaten complex filter uygulandı
        const bool ENABLE_FINAL_FILTER = false; // Crash'i önlemek için kapalı

        std::vector<float> filteredVideoSignal;

        if (ENABLE_FINAL_FILTER) {
            try {
                float videoCutoffFrequency = 4.5e6 / decimationFactor;
                filteredVideoSignal = lowPassFilter(videoSignal, videoCutoffFrequency);
                qDebug() << "  Result size:" << filteredVideoSignal.size();
            }
            catch (...) {
                qCritical() << "Filter failed, using unfiltered signal";
                filteredVideoSignal = videoSignal;
            }
        } else {
            qDebug() << "  Final filter DISABLED (already filtered in Step 2)";
            filteredVideoSignal = videoSignal;
        }

        // Step 11: Convert to image
        qDebug() << "Step 11: Convert to image...";
        float brightness = 0.0f;
        float contrast = 1.2f;
        QImage image = convertToImage(filteredVideoSignal, brightness, contrast);
        frame.image = image;

        qDebug() << "  Image created:" << image.width() << "x" << image.height();
        qDebug() << "=== PAL Demodulation Complete ===\n";

        // Audio processing (simplified for now)
        auto shiftedAudio = frequencyShift(samples, AUDIO_CARRIER);
        frame.audio = fmDemodulateYDiff(shiftedAudio);

    }
    catch (const std::exception& e) {
        qCritical() << "EXCEPTION in PAL demodulation:" << e.what();
        qCritical() << "Creating blank frame";
        frame.image = QImage(PIXELS_PER_LINE, VISIBLE_LINES, QImage::Format_Grayscale8);
        frame.image.fill(Qt::black);
    }
    catch (...) {
        qCritical() << "UNKNOWN EXCEPTION in PAL demodulation!";
        frame.image = QImage(PIXELS_PER_LINE, VISIBLE_LINES, QImage::Format_Grayscale8);
        frame.image.fill(Qt::black);
    }

    return frame;
}

std::vector<std::complex<float>> PALBDemodulator::frequencyShift(
    const std::vector<std::complex<float>>& signal,
    double shiftFreq)
{
    std::vector<std::complex<float>> shifted(signal.size());

    // NEGATIVE frequency shift to move carrier DOWN to baseband
    double phaseIncrement = -2.0 * M_PI * shiftFreq / sampleRate;
    std::complex<float> phase(1.0f, 0.0f);
    std::complex<float> phaseStep(std::cos(phaseIncrement), std::sin(phaseIncrement));

    for (size_t i = 0; i < signal.size(); ++i) {
        shifted[i] = signal[i] * phase;
        phase *= phaseStep;

        // Normalize phase periodically to prevent numerical drift
        if (i % 10000 == 0) {
            float mag = std::abs(phase);
            if (mag > 0.0f) {
                phase /= mag;
            }
        }
    }

    return shifted;
}

std::vector<float> PALBDemodulator::designLowPassFIR(
    int numTaps,
    float cutoffFreq,
    float sampleRate)
{
    qDebug() << "Designing FIR filter: taps=" << numTaps
             << "cutoff=" << cutoffFreq
             << "sampleRate=" << sampleRate;

    if (numTaps <= 0 || numTaps > 1000) {
        qWarning() << "Invalid numTaps:" << numTaps;
        return std::vector<float>();
    }

    if (cutoffFreq <= 0 || cutoffFreq >= sampleRate / 2) {
        qWarning() << "Invalid cutoff frequency:" << cutoffFreq;
        return std::vector<float>();
    }

    try {
        std::vector<float> h(numTaps, 0.0f);

        float fc = cutoffFreq / sampleRate; // Normalized cutoff frequency
        int center = numTaps / 2;

        qDebug() << "  Normalized cutoff:" << fc << "center:" << center;

        // Design sinc filter (ideal low-pass filter)
        for (int i = 0; i < numTaps; ++i) {
            int n = i - center;
            if (n == 0) {
                h[i] = 2.0f * fc;
            } else {
                h[i] = std::sin(2.0f * M_PI * fc * n) / (M_PI * n);
            }

            // Apply Hamming window to reduce ringing
            float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (numTaps - 1));
            h[i] *= window;
        }

        // Normalize coefficients so that DC gain = 1
        float sum = 0.0f;
        for (const auto& coef : h) {
            sum += coef;
        }

        if (sum != 0.0f && !std::isnan(sum) && !std::isinf(sum)) {
            for (auto& coef : h) {
                coef /= sum;
            }
            qDebug() << "  Filter normalized, sum was:" << sum;
        } else {
            qWarning() << "  Filter normalization skipped, invalid sum:" << sum;
        }

        qDebug() << "FIR filter design complete";
        return h;
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in designLowPassFIR:" << e.what();
        return std::vector<float>();
    }
}

std::vector<std::complex<float>> PALBDemodulator::complexLowPassFilter(
    const std::vector<std::complex<float>>& signal,
    float cutoffFreq)
{
    if (signal.empty()) {
        qWarning() << "complexLowPassFilter: Empty signal!";
        return signal;
    }

    qDebug() << "complexLowPassFilter: Input size=" << signal.size() << "cutoff=" << cutoffFreq;

    // Daha kısa filter kullan
    const int numTaps = 51; // 101 yerine 51

    try {
        auto h = designLowPassFIR(numTaps, cutoffFreq, sampleRate);

        if (h.empty() || h.size() != numTaps) {
            qWarning() << "Filter design failed! h.size=" << h.size();
            return signal;
        }

        qDebug() << "Complex filter coefficients created, numTaps=" << numTaps;

        int center = numTaps / 2;

        // Sadece valid range için filtre uygula
        size_t validStart = center;
        size_t validEnd = signal.size() > center ? signal.size() - center : 0;

        if (validEnd <= validStart) {
            qWarning() << "Signal too short for complex filtering, returning original";
            return signal;
        }

        qDebug() << "Valid complex filter range:" << validStart << "to" << validEnd;

        std::vector<std::complex<float>> filtered = signal; // Başlangıçta kopyala

        // Apply FIR filter with careful bounds checking
        for (size_t i = validStart; i < validEnd; ++i) {
            std::complex<float> sum(0.0f, 0.0f);
            bool validSample = true;

            for (int j = 0; j < numTaps; ++j) {
                size_t idx = i - center + j;

                // Çift kontrol
                if (idx >= signal.size()) {
                    validSample = false;
                    break;
                }

                sum += signal[idx] * h[j];
            }

            if (validSample) {
                filtered[i] = sum;
            }

            // İlerleme göster
            if (i % 50000 == 0) {
                qDebug() << "  Complex filtering progress:" << (i * 100 / validEnd) << "%";
            }
        }

        qDebug() << "Complex low-pass filter applied successfully";
        return filtered;
    }
    catch (const std::bad_alloc& e) {
        qCritical() << "OUT OF MEMORY in complexLowPassFilter!" << e.what();
        return signal;
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in complexLowPassFilter:" << e.what();
        return signal;
    }
}

std::vector<float> PALBDemodulator::lowPassFilter(
    const std::vector<float>& signal,
    float cutoffFreq)
{
    if (signal.empty()) {
        qWarning() << "lowPassFilter: Empty signal!";
        return signal;
    }

    qDebug() << "lowPassFilter: Input size=" << signal.size() << "cutoff=" << cutoffFreq;

    // Daha kısa filter kullan - bellek ve performans için
    const int numTaps = 51; // 101 yerine 51 (daha hızlı ve güvenli)

    try {
        auto h = designLowPassFIR(numTaps, cutoffFreq, sampleRate);

        if (h.empty() || h.size() != numTaps) {
            qWarning() << "Filter design failed! h.size=" << h.size();
            return signal;
        }

        qDebug() << "Filter coefficients created, numTaps=" << numTaps;

        int center = numTaps / 2;
        qDebug() << "Filter center=" << center;

        // Sadece valid range için filtre uygula
        size_t validStart = center;
        size_t validEnd = signal.size() > center ? signal.size() - center : 0;

        if (validEnd <= validStart) {
            qWarning() << "Signal too short for filtering, returning original";
            return signal;
        }

        qDebug() << "Valid filter range:" << validStart << "to" << validEnd;

        std::vector<float> filtered = signal; // Başlangıçta kopyala

        // Apply FIR filter with careful bounds checking
        for (size_t i = validStart; i < validEnd; ++i) {
            float sum = 0.0f;
            bool validSample = true;

            for (int j = 0; j < numTaps; ++j) {
                size_t idx = i - center + j;

                // Çift kontrol
                if (idx >= signal.size()) {
                    validSample = false;
                    break;
                }

                sum += signal[idx] * h[j];
            }

            if (validSample) {
                filtered[i] = sum;
            }

            // İlerleme göster (büyük sinyaller için)
            if (i % 50000 == 0) {
                qDebug() << "  Filtering progress:" << (i * 100 / validEnd) << "%";
            }
        }

        qDebug() << "Low-pass filter applied successfully, cutoff:" << cutoffFreq;
        return filtered;
    }
    catch (const std::bad_alloc& e) {
        qCritical() << "OUT OF MEMORY in lowPassFilter!" << e.what();
        return signal;
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in lowPassFilter:" << e.what();
        return signal;
    }
}

std::vector<float> PALBDemodulator::decimate(
    const std::vector<float>& signal,
    int factor)
{
    if (factor <= 1) {
        return signal;
    }

    if (signal.empty()) {
        qWarning() << "decimate: Empty signal!";
        return signal;
    }

    std::vector<float> decimated;
    decimated.reserve(signal.size() / factor + 1);

    try {
        for (size_t i = 0; i < signal.size(); i += factor) {
            decimated.push_back(signal[i]);
        }
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in decimate:" << e.what();
        return signal;
    }

    qDebug() << "Decimated from" << signal.size() << "to" << decimated.size();
    return decimated;
}

std::vector<float> PALBDemodulator::fmDemodulateYDiff(
    const std::vector<std::complex<float>>& signal)
{
    std::vector<float> demodulated(signal.size());

    for (size_t i = 0; i < signal.size(); ++i)
    {
        float sampleNorm = std::abs(signal[i]);
        if (sampleNorm < 1e-10f) {
            demodulated[i] = 0.0f;
            continue;
        }

        float sampleNormI = signal[i].real() / sampleNorm;
        float sampleNormQ = signal[i].imag() / sampleNorm;

        float sample = m_fltBufferI[0] * (sampleNormQ - m_fltBufferQ[1]);
        sample -= m_fltBufferQ[0] * (sampleNormI - m_fltBufferI[1]);

        sample += 2.0f;
        sample /= 4.0f;

        m_fltBufferI[1] = m_fltBufferI[0];
        m_fltBufferQ[1] = m_fltBufferQ[0];

        m_fltBufferI[0] = sampleNormI;
        m_fltBufferQ[0] = sampleNormQ;

        demodulated[i] = sample;
    }

    return demodulated;
}

std::vector<float> PALBDemodulator::amDemodulate(
    const std::vector<std::complex<float>>& signal)
{
    std::vector<float> demodulated(signal.size());

    for (size_t i = 0; i < signal.size(); ++i)
    {
        // For AM demodulation, compute the magnitude (envelope detection)
        demodulated[i] = std::abs(signal[i]);
    }

    return demodulated;
}

std::vector<float> PALBDemodulator::removeDCOffset(
    const std::vector<float>& signal)
{
    if (signal.empty()) {
        return signal;
    }

    float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) / signal.size();
    std::vector<float> corrected(signal.size());

    std::transform(signal.begin(), signal.end(), corrected.begin(),
                   [mean](float sample) { return sample - mean; });

    return corrected;
}

std::vector<float> PALBDemodulator::applyAGC(
    const std::vector<float>& signal)
{
    const float targetAmplitude = 0.7f;
    const float agcAttackRate = 0.001f;  // Slower attack for more stable AGC
    const float agcDecayRate = 0.0001f;   // Even slower decay

    std::vector<float> agcSignal(signal.size());
    float currentGain = 1.0f;

    for (size_t i = 0; i < signal.size(); ++i) {
        float absValue = std::abs(signal[i]);

        // Calculate desired gain
        float desiredGain = targetAmplitude / std::max(absValue, 1e-6f);

        // Smoothly adjust gain
        if (currentGain > desiredGain) {
            // Fast attack when signal is too strong
            currentGain += agcAttackRate * (desiredGain - currentGain);
        } else {
            // Slow decay when signal is weak
            currentGain += agcDecayRate * (desiredGain - currentGain);
        }

        // Limit gain to reasonable range
        currentGain = std::clamp(currentGain, 0.1f, 10.0f);

        agcSignal[i] = signal[i] * currentGain;
    }

    return agcSignal;
}

bool PALBDemodulator::detectVerticalSync(
    const std::vector<float>& signal,
    size_t& syncStart)
{
    if (signal.size() < 1000) {
        qWarning() << "Signal too short for sync detection";
        return false;
    }

    // Find signal statistics to set adaptive threshold
    float minVal = *std::min_element(signal.begin(), signal.end());
    float maxVal = *std::max_element(signal.begin(), signal.end());
    float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) / signal.size();

    qDebug() << "Sync detection - Min:" << minVal << "Max:" << maxVal << "Mean:" << mean;

    // Adaptive threshold (sync pulses are typically at the lowest ~10% of signal)
    float syncThreshold = minVal + (maxVal - minVal) * 0.15f;

    qDebug() << "Using sync threshold:" << syncThreshold;

    const size_t minSyncDuration = static_cast<size_t>(sampleRate * 27e-6); // 27 µs hsync
    const size_t maxSyncDuration = static_cast<size_t>(sampleRate * 160e-6); // Max sync duration

    qDebug() << "Min sync duration:" << minSyncDuration << "samples";

    for (size_t i = 0; i < signal.size() - maxSyncDuration; ++i) {
        if (signal[i] < syncThreshold) {
            size_t lowSamples = 0;

            // Count consecutive samples below threshold
            while (i + lowSamples < signal.size() &&
                   signal[i + lowSamples] < syncThreshold) {
                ++lowSamples;
            }

            // Check if this looks like a sync pulse
            if (lowSamples >= minSyncDuration) {
                syncStart = i;
                qDebug() << "Sync found at sample" << i << "with duration" << lowSamples;
                return true;
            }

            i += lowSamples;
        }
    }

    qDebug() << "No sync pulse found in signal";
    syncStart = 0;
    return false;
}

std::vector<float> PALBDemodulator::removeVBI(
    const std::vector<float>& signal)
{
    const size_t vbiLines = 25; // Typical number of VBI lines in PAL
    const double samplesPerLine = sampleRate * LINE_DURATION;
    const size_t vbiSamples = static_cast<size_t>(vbiLines * samplesPerLine);

    qDebug() << "removeVBI: signal size=" << signal.size()
             << "vbiSamples=" << vbiSamples
             << "samplesPerLine=" << samplesPerLine;

    if (signal.size() <= vbiSamples) {
        qWarning() << "Signal too short to remove VBI, returning original";
        return signal;
    }

    if (vbiSamples >= signal.size()) {
        qWarning() << "VBI samples >= signal size, returning original";
        return signal;
    }

    try {
        std::vector<float> result(signal.begin() + vbiSamples, signal.end());
        qDebug() << "VBI removed, new size:" << result.size();
        return result;
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in removeVBI:" << e.what();
        return signal;
    }
}

std::vector<float> PALBDemodulator::timingRecovery(
    const std::vector<float>& signal)
{
    if (signal.empty()) {
        qWarning() << "timingRecovery: Empty signal!";
        return signal;
    }

    // Calculate actual samples per line at current sample rate
    const double samplesPerLine = sampleRate * LINE_DURATION;
    const double targetSamplesPerLine = PIXELS_PER_LINE;

    qDebug() << "Timing recovery - samples per line:" << samplesPerLine
             << "target:" << targetSamplesPerLine;

    // Estimate number of lines in the signal
    const size_t numLines = static_cast<size_t>(signal.size() / samplesPerLine);

    if (numLines == 0) {
        qWarning() << "Not enough samples for even one line";
        return signal;
    }

    qDebug() << "Estimated lines:" << numLines;

    std::vector<float> recovered;

    try {
        // Reserve with safety margin
        size_t reserveSize = numLines * PIXELS_PER_LINE;
        if (reserveSize > 10000000) {  // Safety check: max 10M samples
            qWarning() << "Reserve size too large, limiting";
            reserveSize = 10000000;
        }
        recovered.reserve(reserveSize);

        // Resample each line to exactly PIXELS_PER_LINE samples
        for (size_t line = 0; line < numLines; ++line) {
            size_t lineStart = static_cast<size_t>(line * samplesPerLine);

            // Safety check
            if (lineStart >= signal.size()) {
                qWarning() << "lineStart" << lineStart << ">= signal.size" << signal.size();
                break;
            }

            for (int pixel = 0; pixel < PIXELS_PER_LINE; ++pixel) {
                // Calculate source position within this line
                double srcPos = lineStart + (pixel * samplesPerLine / PIXELS_PER_LINE);
                size_t idx = static_cast<size_t>(srcPos);

                // Bounds check
                if (idx + 1 >= signal.size()) {
                    if (idx < signal.size()) {
                        recovered.push_back(signal[idx]);
                    }
                    break;
                }

                double frac = srcPos - idx;

                // Linear interpolation
                float interpolated = signal[idx] * (1.0f - frac) +
                                     signal[idx + 1] * frac;
                recovered.push_back(interpolated);
            }
        }
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in timingRecovery:" << e.what();
        qCritical() << "Signal size was:" << signal.size();
        qCritical() << "Recovered size:" << recovered.size();

        // Return what we have so far
        if (!recovered.empty()) {
            return recovered;
        }
        return signal;
    }

    qDebug() << "Timing recovery complete. Output size:" << recovered.size();
    return recovered;
}

QImage PALBDemodulator::convertToImage(
    const std::vector<float>& videoSignal,
    float brightness,
    float contrast)
{
    QImage image(PIXELS_PER_LINE, VISIBLE_LINES, QImage::Format_Grayscale8);

    // Calculate expected signal size
    size_t expectedSize = PIXELS_PER_LINE * VISIBLE_LINES;

    if (videoSignal.size() < expectedSize) {
        qWarning() << "Video signal too short:" << videoSignal.size()
                   << "expected:" << expectedSize;
        image.fill(Qt::black);
        return image;
    }

    // Find min/max for normalization
    float minVal = *std::min_element(videoSignal.begin(),
                                     videoSignal.begin() + expectedSize);
    float maxVal = *std::max_element(videoSignal.begin(),
                                     videoSignal.begin() + expectedSize);

    if (maxVal == minVal) {
        maxVal = minVal + 1.0f; // Avoid division by zero
    }

    qDebug() << "Image conversion - min:" << minVal << "max:" << maxVal;

    for (int line = 0; line < VISIBLE_LINES; ++line) {
        uchar* scanLine = image.scanLine(line);

        for (int pixel = 0; pixel < PIXELS_PER_LINE; ++pixel) {
            size_t index = line * PIXELS_PER_LINE + pixel;

            if (index < videoSignal.size()) {
                // Normalize to 0-1 range
                float normalized = (videoSignal[index] - minVal) / (maxVal - minVal);

                // Apply contrast and brightness
                normalized = (normalized - 0.5f) * contrast + 0.5f + brightness;

                // Convert to 0-255 and clamp
                int value = static_cast<int>(normalized * 255.0f);
                value = std::clamp(value, 0, 255);

                scanLine[pixel] = static_cast<uchar>(value);
            } else {
                scanLine[pixel] = 0; // Black for missing data
            }
        }
    }

    return image;
}
