#include "palbdemodulator.h"
#include <cmath>

PALBDemodulator::PALBDemodulator(double _sampleRate, QObject *parent)
    : QObject(parent), sampleRate(_sampleRate)
{
    if (sampleRate < 16e6) {
        qWarning() << "Sample rate is less than 16 MHz. Adjusting processing accordingly.";
        sampleRate = 16e6;
    }
}

PALBDemodulator::DemodulatedFrame PALBDemodulator::demodulate(const std::vector<std::complex<float>>& samples)
{
    DemodulatedFrame frame;

    // Step 1: Frequency shift to isolate the video carrier
    auto shiftedVideo = frequencyShift(samples, -VIDEO_CARRIER);

    // Step 2: AM demodulate the shifted video signal
    auto videoSignal = amDemodulate(shiftedVideo);

    // New steps: DC offset removal and AGC
    videoSignal = removeDCOffset(videoSignal);
    videoSignal = applyAGC(videoSignal);

    // New step: Vertical sync detection and alignment
    size_t syncStart;
    if (detectVerticalSync(videoSignal, syncStart)) {
        videoSignal = std::vector<float>(videoSignal.begin() + syncStart, videoSignal.end());
    }

    // New step: Remove Vertical Blanking Interval
    videoSignal = removeVBI(videoSignal);

    // New step: Timing recovery
    videoSignal = timingRecovery(videoSignal);

    // Step 3: Apply a low-pass filter to the demodulated video signal
    float videoCutoffFrequency = 4.5e6; // Typically, PAL video bandwidth is around 4.5 MHz
    auto filteredVideoSignal = lowPassFilter(videoSignal, videoCutoffFrequency);

    // Step 4: Generate the video frame from the filtered video signal
    frame.image = convertToImage(filteredVideoSignal);

    // Step 5: Frequency shift to isolate the audio carrier
    //auto shiftedAudio = frequencyShift(samples, -AUDIO_CARRIER);

    // Step 6: FM demodulate the audio signal
    //auto audioSignal = fmDemodulate(shiftedAudio);

    // Optional: Apply a low-pass filter to the demodulated audio signal
    //float audioCutoffFrequency = 15e3; // Standard audio cutoff for PAL is 15 kHz
    //frame.audio = lowPassFilter(audioSignal, audioCutoffFrequency);

    return frame;
}

std::vector<std::complex<float>> PALBDemodulator::frequencyShift(const std::vector<std::complex<float>>& signal, double shiftFreq)
{
    std::vector<std::complex<float>> shifted(signal.size());
    double phaseIncrement = 2 * M_PI * shiftFreq / sampleRate;
    std::complex<float> phase(1, 0);
    std::complex<float> phaseStep(cos(phaseIncrement), sin(phaseIncrement));

    for (size_t i = 0; i < signal.size(); ++i) {
        shifted[i] = signal[i] * phase;
        phase *= phaseStep;
    }

    return shifted;
}

std::vector<float> PALBDemodulator::amDemodulate(const std::vector<std::complex<float>>& signal)
{
    std::vector<float> demodulated(signal.size());
    std::transform(signal.begin(), signal.end(), demodulated.begin(),
                   [](std::complex<float> sample) { return std::abs(sample); });
    return demodulated;
}

std::vector<float> PALBDemodulator::fmDemodulate(const std::vector<std::complex<float>>& signal)
{
    std::vector<float> demodulated(signal.size());
    for (size_t i = 1; i < signal.size(); ++i) {
        float phaseDiff = std::arg(signal[i] * std::conj(signal[i - 1]));
        demodulated[i] = phaseDiff;
    }
    return demodulated;
}

std::vector<float> PALBDemodulator::lowPassFilter(const std::vector<float>& signal, float cutoffFreq)
{
    int numTaps = 101; // Adjust based on the desired filter sharpness and latency
    std::vector<float> coefficients = generateLowPassCoefficients(sampleRate, cutoffFreq, numTaps);

    int signalSize = signal.size();
    std::vector<float> filtered(signalSize, 0.0f);

    for (int i = 0; i < signalSize; ++i) {
        float acc = 0.0f;
        for (int j = 0; j < numTaps; ++j) {
            if (i - j >= 0) {
                acc += signal[i - j] * coefficients[j];
            }
        }
        filtered[i] = acc;
    }

    return filtered;
}

std::vector<std::complex<float>> PALBDemodulator::extractColorSignal(const std::vector<float>& videoSignal)
{
    std::vector<std::complex<float>> colorSignal(videoSignal.size());
    double phaseIncrement = 2 * M_PI * COLOR_SUBCARRIER / sampleRate;
    std::complex<float> oscillator(1, 0);
    std::complex<float> stepFactor(std::cos(phaseIncrement), std::sin(phaseIncrement));

    for (size_t i = 0; i < videoSignal.size(); ++i) {
        colorSignal[i] = videoSignal[i] * oscillator;
        oscillator *= stepFactor;
    }

    return colorSignal;
}

std::vector<float> PALBDemodulator::demodulateU(const std::vector<std::complex<float>>& colorSignal)
{
    std::vector<float> uSignal(colorSignal.size());
    for (size_t i = 0; i < colorSignal.size(); ++i) {
        uSignal[i] = colorSignal[i].real();
    }
    return lowPassFilter(uSignal, 1.3e6);  // U bandwidth is typically around 1.3 MHz
}

std::vector<float> PALBDemodulator::demodulateV(const std::vector<std::complex<float>>& colorSignal)
{
    std::vector<float> vSignal(colorSignal.size());
    for (size_t i = 0; i < colorSignal.size(); ++i) {
        vSignal[i] = colorSignal[i].imag();
    }
    return lowPassFilter(vSignal, 1.3e6);  // V bandwidth is typically around 1.3 MHz
}

QRgb PALBDemodulator::yuv2rgb(float y, float u, float v)
{
    // Convert YUV to RGB
    int r = std::clamp(static_cast<int>((y + 1.140f * v) * 255), 0, 255);
    int g = std::clamp(static_cast<int>((y - 0.395f * u - 0.581f * v) * 255), 0, 255);
    int b = std::clamp(static_cast<int>((y + 2.032f * u) * 255), 0, 255);

    return qRgb(r, g, b);
}

QImage PALBDemodulator::convertToImage(const std::vector<float>& videoSignal)
{
    QImage image(PIXELS_PER_LINE, VISIBLE_LINES, QImage::Format_Grayscale8);
    int pixelsPerLine = PIXELS_PER_LINE;
    for (int line = 0; line < VISIBLE_LINES; ++line) {
        for (int pixel = 0; pixel < pixelsPerLine; ++pixel) {
            size_t index = line * pixelsPerLine + pixel;
            if (index < videoSignal.size()) {
                uint8_t value = static_cast<uint8_t>(std::clamp(videoSignal[index] * 255.0f, 0.0f, 255.0f));
                image.setPixel(pixel, line, qRgb(value, value, value));
            }
        }
    }
    return image;

    /*QImage image(PIXELS_PER_LINE, VISIBLE_LINES, QImage::Format_RGB32);
    int pixelsPerLine = PIXELS_PER_LINE;

    // Extract color information
    auto colorSignal = extractColorSignal(videoSignal);
    auto uSignal = demodulateU(colorSignal);
    auto vSignal = demodulateV(colorSignal);

    for (int line = 0; line < VISIBLE_LINES; ++line) {
        for (int pixel = 0; pixel < pixelsPerLine; ++pixel) {
            size_t index = line * pixelsPerLine + pixel;
            if (index < videoSignal.size()) {
                float y = videoSignal[index];
                float u = uSignal[index];
                float v = vSignal[index];

                QRgb color = yuv2rgb(y, u, v);
                image.setPixel(pixel, line, color);
            }
        }
    }
    return image;*/
}

std::vector<float> PALBDemodulator::generateLowPassCoefficients(float sampleRate, float cutoffFreq, int numTaps)
{
    std::vector<float> coeffs(numTaps);
    float normCutoff = cutoffFreq / (sampleRate / 2); // Normalized cutoff frequency (0.0 to 1.0)

    for (int i = 0; i < numTaps; ++i) {
        int middle = numTaps / 2;
        if (i == middle) {
            coeffs[i] = normCutoff;
        } else {
            float x = static_cast<float>(i - middle) * M_PI;
            coeffs[i] = sin(normCutoff * x) / x;
        }
        // Apply Hamming window
        coeffs[i] *= 0.54f - 0.46f * cos(2.0f * M_PI * i / (numTaps - 1));
    }

    // Normalize coefficients
    float sum = std::accumulate(coeffs.begin(), coeffs.end(), 0.0f);
    for (auto& c : coeffs) c /= sum;

    return coeffs;
}

// New helper function implementations

bool PALBDemodulator::detectVerticalSync(const std::vector<float>& signal, size_t& syncStart)
{
    // Simple vertical sync detection based on signal level
    // Adjust these thresholds based on your specific signal characteristics
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
    // For more advanced timing recovery, consider implementing a PLL-based approach
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
    const float targetAmplitude = 0.7f; // Adjust as needed
    const float agcAttackRate = 0.01f; // Adjust for faster/slower AGC response

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
