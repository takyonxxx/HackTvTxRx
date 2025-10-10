#include "palbdemodulator.h"
#include <cmath>
#include <algorithm>
#include <numeric>

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
        if (samples.empty()) {
            return frame;
        }

        // Step 1: Frequency shift to isolate the video carrier
        auto shiftedVideo = frequencyShift(samples, VIDEO_CARRIER);

        // Step 2: Apply complex low-pass filter to remove unwanted frequencies
        float videoBandwidth = 5.5e6;
        shiftedVideo = complexLowPassFilter(shiftedVideo, videoBandwidth);

        // Step 3: AM demodulate (envelope detection)
        auto videoSignal = amDemodulate(shiftedVideo);

        // Step 4: Decimate to reduce sample rate
        int decimationFactor = 2;
        videoSignal = decimate(videoSignal, decimationFactor);
        double workingSampleRate = sampleRate / decimationFactor;

        // Step 5: Remove DC offset
        videoSignal = removeDCOffset(videoSignal);

        // Check if signal might be inverted
        if (!videoSignal.empty()) {
            float sum = std::accumulate(videoSignal.begin(), videoSignal.end(), 0.0f);
            float mean = sum / videoSignal.size();

            if (mean < -0.1f) {
                for (auto& sample : videoSignal) {
                    sample = -sample;
                }
            }
        }

        // Step 6: Apply AGC
        videoSignal = applyAGC(videoSignal);

        // Step 7: Vertical sync detection and alignment
        size_t syncStart = 0;
        double savedRate = sampleRate;
        sampleRate = workingSampleRate;

        if (detectVerticalSync(videoSignal, syncStart)) {
            std::vector<float> syncedSignal(videoSignal.begin() + syncStart, videoSignal.end());
            videoSignal = syncedSignal;
        }

        // Step 8: Remove Vertical Blanking Interval
        videoSignal = removeVBI(videoSignal);

        // Step 9: Timing recovery - resample to correct pixel rate
        videoSignal = timingRecovery(videoSignal);
        sampleRate = savedRate;

        // Step 10: Skip final filter (already filtered in Step 2)
        std::vector<float> filteredVideoSignal = videoSignal;

        // Step 11: Convert to image
        float brightness = 0.0f;
        float contrast = 1.2f;
        QImage image = convertToImage(filteredVideoSignal, brightness, contrast);
        frame.image = image;

        // Audio processing
        auto shiftedAudio = frequencyShift(samples, AUDIO_CARRIER);
        frame.audio = fmDemodulateYDiff(shiftedAudio);

    }
    catch (const std::exception& e) {
        qCritical() << "EXCEPTION in PAL demodulation:" << e.what();
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

    double phaseIncrement = -2.0 * M_PI * shiftFreq / sampleRate;
    std::complex<float> phase(1.0f, 0.0f);
    std::complex<float> phaseStep(std::cos(phaseIncrement), std::sin(phaseIncrement));

    for (size_t i = 0; i < signal.size(); ++i) {
        shifted[i] = signal[i] * phase;
        phase *= phaseStep;

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
    if (numTaps <= 0 || numTaps > 1000) {
        return std::vector<float>();
    }

    if (cutoffFreq <= 0 || cutoffFreq >= sampleRate / 2) {
        return std::vector<float>();
    }

    try {
        std::vector<float> h(numTaps, 0.0f);

        float fc = cutoffFreq / sampleRate;
        int center = numTaps / 2;

        for (int i = 0; i < numTaps; ++i) {
            int n = i - center;
            if (n == 0) {
                h[i] = 2.0f * fc;
            } else {
                h[i] = std::sin(2.0f * M_PI * fc * n) / (M_PI * n);
            }

            float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (numTaps - 1));
            h[i] *= window;
        }

        float sum = 0.0f;
        for (const auto& coef : h) {
            sum += coef;
        }

        if (sum != 0.0f && !std::isnan(sum) && !std::isinf(sum)) {
            for (auto& coef : h) {
                coef /= sum;
            }
        }

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
        return signal;
    }

    const int numTaps = 51;

    try {
        auto h = designLowPassFIR(numTaps, cutoffFreq, sampleRate);

        if (h.empty() || h.size() != numTaps) {
            return signal;
        }

        int center = numTaps / 2;
        size_t validStart = center;
        size_t validEnd = signal.size() > center ? signal.size() - center : 0;

        if (validEnd <= validStart) {
            return signal;
        }

        std::vector<std::complex<float>> filtered = signal;

        for (size_t i = validStart; i < validEnd; ++i) {
            std::complex<float> sum(0.0f, 0.0f);
            bool validSample = true;

            for (int j = 0; j < numTaps; ++j) {
                size_t idx = i - center + j;

                if (idx >= signal.size()) {
                    validSample = false;
                    break;
                }

                sum += signal[idx] * h[j];
            }

            if (validSample) {
                filtered[i] = sum;
            }
        }

        return filtered;
    }
    catch (const std::bad_alloc& e) {
        qCritical() << "OUT OF MEMORY in complexLowPassFilter:" << e.what();
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
        return signal;
    }

    const int numTaps = 51;

    try {
        auto h = designLowPassFIR(numTaps, cutoffFreq, sampleRate);

        if (h.empty() || h.size() != numTaps) {
            return signal;
        }

        int center = numTaps / 2;
        size_t validStart = center;
        size_t validEnd = signal.size() > center ? signal.size() - center : 0;

        if (validEnd <= validStart) {
            return signal;
        }

        std::vector<float> filtered = signal;

        for (size_t i = validStart; i < validEnd; ++i) {
            float sum = 0.0f;
            bool validSample = true;

            for (int j = 0; j < numTaps; ++j) {
                size_t idx = i - center + j;

                if (idx >= signal.size()) {
                    validSample = false;
                    break;
                }

                sum += signal[idx] * h[j];
            }

            if (validSample) {
                filtered[i] = sum;
            }
        }

        return filtered;
    }
    catch (const std::bad_alloc& e) {
        qCritical() << "OUT OF MEMORY in lowPassFilter:" << e.what();
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
    const float agcAttackRate = 0.001f;
    const float agcDecayRate = 0.0001f;

    std::vector<float> agcSignal(signal.size());
    float currentGain = 1.0f;

    for (size_t i = 0; i < signal.size(); ++i) {
        float absValue = std::abs(signal[i]);
        float desiredGain = targetAmplitude / std::max(absValue, 1e-6f);

        if (currentGain > desiredGain) {
            currentGain += agcAttackRate * (desiredGain - currentGain);
        } else {
            currentGain += agcDecayRate * (desiredGain - currentGain);
        }

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
        return false;
    }

    float minVal = *std::min_element(signal.begin(), signal.end());
    float maxVal = *std::max_element(signal.begin(), signal.end());
    float syncThreshold = minVal + (maxVal - minVal) * 0.15f;

    const size_t minSyncDuration = static_cast<size_t>(sampleRate * 27e-6);
    const size_t maxSyncDuration = static_cast<size_t>(sampleRate * 160e-6);

    for (size_t i = 0; i < signal.size() - maxSyncDuration; ++i) {
        if (signal[i] < syncThreshold) {
            size_t lowSamples = 0;

            while (i + lowSamples < signal.size() &&
                   signal[i + lowSamples] < syncThreshold) {
                ++lowSamples;
            }

            if (lowSamples >= minSyncDuration) {
                syncStart = i;
                return true;
            }

            i += lowSamples;
        }
    }

    syncStart = 0;
    return false;
}

std::vector<float> PALBDemodulator::removeVBI(
    const std::vector<float>& signal)
{
    const size_t vbiLines = 25;
    const double samplesPerLine = sampleRate * LINE_DURATION;
    const size_t vbiSamples = static_cast<size_t>(vbiLines * samplesPerLine);

    if (signal.size() <= vbiSamples || vbiSamples >= signal.size()) {
        return signal;
    }

    try {
        std::vector<float> result(signal.begin() + vbiSamples, signal.end());
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
        return signal;
    }

    const double samplesPerLine = sampleRate * LINE_DURATION;
    const size_t numLines = static_cast<size_t>(signal.size() / samplesPerLine);

    if (numLines == 0) {
        return signal;
    }

    std::vector<float> recovered;

    try {
        size_t reserveSize = numLines * PIXELS_PER_LINE;
        if (reserveSize > 10000000) {
            reserveSize = 10000000;
        }
        recovered.reserve(reserveSize);

        for (size_t line = 0; line < numLines; ++line) {
            size_t lineStart = static_cast<size_t>(line * samplesPerLine);

            if (lineStart >= signal.size()) {
                break;
            }

            for (int pixel = 0; pixel < PIXELS_PER_LINE; ++pixel) {
                double srcPos = lineStart + (pixel * samplesPerLine / PIXELS_PER_LINE);
                size_t idx = static_cast<size_t>(srcPos);

                if (idx + 1 >= signal.size()) {
                    if (idx < signal.size()) {
                        recovered.push_back(signal[idx]);
                    }
                    break;
                }

                double frac = srcPos - idx;
                float interpolated = signal[idx] * (1.0f - frac) +
                                     signal[idx + 1] * frac;
                recovered.push_back(interpolated);
            }
        }
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in timingRecovery:" << e.what();
        if (!recovered.empty()) {
            return recovered;
        }
        return signal;
    }

    return recovered;
}

QImage PALBDemodulator::convertToImage(
    const std::vector<float>& videoSignal,
    float brightness,
    float contrast)
{
    QImage image(PIXELS_PER_LINE, VISIBLE_LINES, QImage::Format_Grayscale8);

    size_t expectedSize = PIXELS_PER_LINE * VISIBLE_LINES;

    if (videoSignal.size() < expectedSize) {
        image.fill(Qt::black);
        return image;
    }

    float minVal = *std::min_element(videoSignal.begin(),
                                     videoSignal.begin() + expectedSize);
    float maxVal = *std::max_element(videoSignal.begin(),
                                     videoSignal.begin() + expectedSize);

    if (maxVal == minVal) {
        maxVal = minVal + 1.0f;
    }

    for (int line = 0; line < VISIBLE_LINES; ++line) {
        uchar* scanLine = image.scanLine(line);

        for (int pixel = 0; pixel < PIXELS_PER_LINE; ++pixel) {
            size_t index = line * PIXELS_PER_LINE + pixel;

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
