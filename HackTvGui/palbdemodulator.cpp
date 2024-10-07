#include "palbdemodulator.h"
#include <cmath>
#include <algorithm>

PALBDemodulator::PALBDemodulator(double _sampleRate, QObject *parent)
    : QObject(parent), sampleRate(_sampleRate)
{
    // if (sampleRate < 16e6) {
    //     qWarning() << "Sample rate is less than 16 MHz. Adjusting processing accordingly.";
    //     sampleRate = 16e6;
    // }
    m_fltBufferI.fill(0.0f);
    m_fltBufferQ.fill(0.0f);
}

PALBDemodulator::DemodulatedFrame PALBDemodulator::demodulate(const std::vector<std::complex<float>>& samples)
{
    DemodulatedFrame frame;

    // Step 1: Frequency shift to isolate the video carrier
    auto shiftedVideo = frequencyShift(samples, -VIDEO_CARRIER);

    // Step 2: FM demodulate the shifted video signal using YDiff method
    auto videoSignal = fmDemodulateYDiff(shiftedVideo);

    // Step 3: Remove DC offset and apply AGC
    videoSignal = removeDCOffset(videoSignal);
    videoSignal = applyAGC(videoSignal);

    // Step 4: Vertical sync detection and alignment
    size_t syncStart;
    if (detectVerticalSync(videoSignal, syncStart)) {
        videoSignal = std::vector<float>(videoSignal.begin() + syncStart, videoSignal.end());
    }

    // Step 5: Remove Vertical Blanking Interval
    videoSignal = removeVBI(videoSignal);

    // Step 6: Timing recovery
    videoSignal = timingRecovery(videoSignal);

    // Step 7: Apply a low-pass filter to the demodulated video signal
    float videoCutoffFrequency = 4.5e6; // Typically, PAL video bandwidth is around 4.5 MHz
    auto filteredVideoSignal = lowPassFilter(videoSignal, videoCutoffFrequency);

    // Step 8: Generate the video frame from the filtered video signal
    frame.image = convertToImage(filteredVideoSignal);

    // Audio processing (simplified for now)
    auto shiftedAudio = frequencyShift(samples, -AUDIO_CARRIER);
    frame.audio = fmDemodulateYDiff(shiftedAudio);

    return frame;
}

std::vector<std::complex<float>> PALBDemodulator::frequencyShift(const std::vector<std::complex<float>>& signal, double shiftFreq)
{
    std::vector<std::complex<float>> shifted(signal.size());
    double phaseIncrement = 2 * M_PI * shiftFreq / sampleRate;
    std::complex<float> phase(1, 0);
    std::complex<float> phaseStep(std::cos(phaseIncrement), std::sin(phaseIncrement));

    for (size_t i = 0; i < signal.size(); ++i) {
        shifted[i] = signal[i] * phase;
        phase *= phaseStep;
    }

    return shifted;
}

std::vector<float> PALBDemodulator::fmDemodulateYDiff(const std::vector<std::complex<float>>& signal)
{
    std::vector<float> demodulated(signal.size());

    for (size_t i = 0; i < signal.size(); ++i)
    {
        float sampleNorm = std::abs(signal[i]);
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

std::vector<float> PALBDemodulator::lowPassFilter(const std::vector<float>& signal, float cutoffFreq)
{
    // Implement a simple moving average filter for now
    // For better results, implement a proper FIR filter
    const int windowSize = static_cast<int>(sampleRate / cutoffFreq);
    std::vector<float> filtered(signal.size());

    for (size_t i = 0; i < signal.size(); ++i)
    {
        float sum = 0.0f;
        int count = 0;
        for (int j = std::max(0, static_cast<int>(i) - windowSize/2);
             j < std::min(static_cast<int>(signal.size()), static_cast<int>(i) + windowSize/2 + 1); ++j)
        {
            sum += signal[j];
            ++count;
        }
        filtered[i] = sum / count;
    }

    return filtered;
}

std::vector<float> PALBDemodulator::removeDCOffset(const std::vector<float>& signal)
{
    float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) / signal.size();
    std::vector<float> corrected(signal.size());
    std::transform(signal.begin(), signal.end(), corrected.begin(),
                   [mean](float sample) { return sample - mean; });
    return corrected;
}

std::vector<float> PALBDemodulator::applyAGC(const std::vector<float>& signal)
{
    const float targetAmplitude = 0.7f;
    const float agcAttackRate = 0.01f;

    std::vector<float> agcSignal(signal.size());
    float currentGain = 1.0f;

    for (size_t i = 0; i < signal.size(); ++i) {
        float absValue = std::abs(signal[i]);
        if (absValue * currentGain > targetAmplitude) {
            currentGain = targetAmplitude / absValue;
        } else {
            currentGain += agcAttackRate * (targetAmplitude / std::max(absValue, 1e-6f) - currentGain);
        }
        agcSignal[i] = signal[i] * currentGain;
    }

    return agcSignal;
}

bool PALBDemodulator::detectVerticalSync(const std::vector<float>& signal, size_t& syncStart)
{
    // Simple vertical sync detection based on signal level
    const float syncThreshold = 0.1f;
    const size_t minSyncDuration = static_cast<size_t>(sampleRate * 150e-6); // 150 µs sync pulse

    for (size_t i = 0; i < signal.size() - minSyncDuration; ++i) {
        if (signal[i] < syncThreshold) {
            size_t lowSamples = 0;
            while (i + lowSamples < signal.size() && signal[i + lowSamples] < syncThreshold) {
                ++lowSamples;
            }
            if (lowSamples >= minSyncDuration) {
                syncStart = i;
                return true;
            }
            i += lowSamples;
        }
    }
    return false;
}

std::vector<float> PALBDemodulator::removeVBI(const std::vector<float>& signal)
{
    const size_t vbiLines = 25; // Typical number of VBI lines in PAL
    const size_t vbiSamples = vbiLines * PIXELS_PER_LINE;

    if (signal.size() <= vbiSamples) {
        qWarning() << "Signal is too short to remove VBI";
        return signal;
    }

    return std::vector<float>(signal.begin() + vbiSamples, signal.end());
}

std::vector<float> PALBDemodulator::timingRecovery(const std::vector<float>& signal)
{
    // Simple linear interpolation for timing recovery
    const double nominalSamplesPerLine = sampleRate * LINE_DURATION;
    const double resampleRatio = PIXELS_PER_LINE / nominalSamplesPerLine;

    std::vector<float> recovered;
    recovered.reserve(signal.size() * resampleRatio);

    for (size_t i = 0; i < signal.size() - 1; ++i) {
        double t = i * resampleRatio;
        size_t idx = static_cast<size_t>(t);
        double frac = t - idx;

        if (idx + 1 < signal.size()) {
            float interpolated = signal[idx] * (1 - frac) + signal[idx + 1] * frac;
            recovered.push_back(interpolated);
        }
    }

    return recovered;
}

QImage PALBDemodulator::convertToImage(const std::vector<float>& videoSignal)
{
    QImage image(PIXELS_PER_LINE, VISIBLE_LINES, QImage::Format_Grayscale8);

    for (int line = 0; line < VISIBLE_LINES; ++line) {
        for (int pixel = 0; pixel < PIXELS_PER_LINE; ++pixel) {
            size_t index = line * PIXELS_PER_LINE + pixel;
            if (index < videoSignal.size()) {
                uint8_t value = static_cast<uint8_t>(std::clamp(videoSignal[index] * 255.0f, 0.0f, 255.0f));
                image.setPixel(pixel, line, qRgb(value, value, value));
            }
        }
    }

    return image;
}
