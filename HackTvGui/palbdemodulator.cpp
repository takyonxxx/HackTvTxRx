#include "palbdemodulator.h"
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <numeric>

PALBDemodulator::PALBDemodulator(double sampleRate, QObject *parent)
    : QObject(parent), sampleRate(sampleRate), colorBurstPhase(0.0f)
{
    // Ensure sampleRate is within a reasonable range
    if (sampleRate < 1e6 || sampleRate > 100e6) {
        qWarning() << "Invalid sample rate:" << sampleRate << ". Setting to default 20e6.";
        this->sampleRate = 20e6;
    }

    designFilters();
    colorBurstOscillator = std::complex<float>(1.0f, 0.0f);

    // Initialize buffers
    frameBuffer.fill({});
    uBuffer.fill({});
    vBuffer.fill({});
}

PALBDemodulator::DemodulatedFrame PALBDemodulator::demodulate(const std::vector<std::complex<float>>& samples)
{
    DemodulatedFrame frame;

    // Step 1: Demodulate the AM signal
    demodulateAM(samples);

    // Step 2: Extract video and audio signals
    extractVideoSignal();
    extractAudioSignal();

    // Step 3: Process video signal
    synchronizeHorizontal();
    synchronizeVertical();
    decodeColor();

    // Step 4: Create QImage from processed video data
    frame.image = createImage();

    // Step 5: Process audio signal (simplified)
    frame.audio = audioSignal;

    return frame;
}

void PALBDemodulator::demodulateAM(const std::vector<std::complex<float>>& samples)
{
    videoSignal.resize(samples.size());

    for (size_t i = 0; i < samples.size(); ++i) {
        videoSignal[i] = std::abs(samples[i]);
    }
}

void PALBDemodulator::extractVideoSignal()
{
    std::vector<float> filteredVideo(videoSignal.size());
    for (size_t i = 0; i < videoSignal.size(); ++i) {
        filteredVideo[i] = applyFilter(videoFilter, videoSignal, i);
    }

    // Normalize the filtered signal
    float maxAmplitude = *std::max_element(filteredVideo.begin(), filteredVideo.end());
    float minAmplitude = *std::min_element(filteredVideo.begin(), filteredVideo.end());
    float range = maxAmplitude - minAmplitude;

    if (range > 0) {
        for (size_t i = 0; i < filteredVideo.size(); ++i) {
            filteredVideo[i] = (filteredVideo[i] - minAmplitude) / range;
        }
    } else {
        qWarning() << "Zero range in video signal normalization";
    }

    videoSignal = filteredVideo;
}

void PALBDemodulator::extractAudioSignal()
{
    // Apply audio bandpass filter
    audioSignal.resize(videoSignal.size());
    for (size_t i = 0; i < videoSignal.size(); ++i) {
        audioSignal[i] = applyFilter(audioFilter, videoSignal, i);
    }
}

void PALBDemodulator::synchronizeHorizontal()
{
    int64_t samplesPerLine = static_cast<int64_t>(std::round(LINE_DURATION * sampleRate));
    int currentLine = 0;

    for (size_t i = 0; i < videoSignal.size(); i += samplesPerLine) {
        // Find sync pulse
        int syncStart = i;
        float minValue = 1.0f;
        for (int j = 0; j < samplesPerLine && (i + j) < videoSignal.size(); ++j) {
            if (videoSignal[i + j] < minValue) {
                minValue = videoSignal[i + j];
                syncStart = i + j;
            }
        }

        // Extract active video region
        int activeStart = syncStart + static_cast<int>(0.005 * sampleRate);  // 5µs after sync
        for (int pixel = 0; pixel < PIXELS_PER_LINE; ++pixel) {
            int sampleIndex = activeStart + (pixel * samplesPerLine / PIXELS_PER_LINE);
            if (sampleIndex < videoSignal.size()) {
                frameBuffer[currentLine][pixel] = videoSignal[sampleIndex];
            } else {
                frameBuffer[currentLine][pixel] = 0.0f;
            }
        }
        currentLine = (currentLine + 1) % LINES_PER_FRAME;
    }
}

void PALBDemodulator::synchronizeVertical()
{
    // Detect vertical sync pulses
    // This is a simplified version and may need refinement for real-world signals
    int samplesPerField = static_cast<int>(FIELD_DURATION * sampleRate);
    for (int i = 0; i < videoSignal.size() - samplesPerField; i++) {
        int longPulseCount = 0;
        for (int j = 0; j < 5; j++) {  // Check for 5 long pulses
            if (videoSignal[i + j * static_cast<int>(LINE_DURATION * sampleRate)] < 0.1f) {
                longPulseCount++;
            }
        }
        if (longPulseCount >= 4) {  // Found vertical sync
            // Adjust frame start
            std::rotate(frameBuffer.begin(), frameBuffer.begin() + (i / static_cast<int>(LINE_DURATION * sampleRate)), frameBuffer.end());
            break;
        }
    }
}

void PALBDemodulator::decodeColor()
{
    int64_t samplesPerLine = static_cast<int64_t>(std::round(LINE_DURATION * sampleRate));
    if (samplesPerLine <= 0 || samplesPerLine > static_cast<int64_t>(std::numeric_limits<int>::max())) {
        qWarning() << "Invalid samplesPerLine value:" << samplesPerLine;
        return;
    }

    // Adjust these calculations to use microseconds
    constexpr double BURST_START_US = 5.0;  // 5µs after sync
    constexpr double BURST_END_US = 7.5;    // 7.5µs after sync

    int burstStart = static_cast<int>(std::round((BURST_START_US * 1e-6) * sampleRate));
    int burstEnd = static_cast<int>(std::round((BURST_END_US * 1e-6) * sampleRate));

    if (burstStart >= burstEnd || burstEnd > PIXELS_PER_LINE) {
        qWarning() << "Invalid burst start/end values:" << burstStart << burstEnd;
        return;
    }

    bool colorBurstDetected = false;
    for (int line = 0; line < LINES_PER_FRAME; ++line) {
        std::complex<float> burst(0.0f, 0.0f);
        for (int i = burstStart; i < burstEnd; ++i) {
            if (i < 0 || i >= PIXELS_PER_LINE) {
                qWarning() << "Invalid index in color burst extraction:" << i;
                continue;
            }
            float sample = frameBuffer[line][i];
            burst += std::complex<float>(sample * std::cos(colorBurstPhase), sample * std::sin(colorBurstPhase));
            colorBurstPhase += 2 * M_PI * COLOR_SUBCARRIER / sampleRate;
        }

        if (burstEnd > burstStart) {
            burst /= static_cast<float>(burstEnd - burstStart);
        }

        float burstMagnitude = std::abs(burst);
        if (burstMagnitude > 1e-6f) {
            colorBurstOscillator = std::conj(burst) / burstMagnitude;
            colorBurstDetected = true;
        } else {
            colorBurstOscillator = std::complex<float>(1.0f, 0.0f);
        }

        for (int pixel = 0; pixel < PIXELS_PER_LINE; ++pixel) {
            float y = frameBuffer[line][pixel];
            if (colorBurstDetected) {
                std::complex<float> color = std::complex<float>(y, 0.0f) * colorBurstOscillator;
                uBuffer[line][pixel] = color.real();
                vBuffer[line][pixel] = color.imag() * (line % 2 == 0 ? 1.0f : -1.0f);  // PAL switch
                colorBurstOscillator *= std::polar(1.0f, static_cast<float>(2 * M_PI * COLOR_SUBCARRIER / sampleRate));
            } else {
                // If no color burst detected, treat as grayscale
                uBuffer[line][pixel] = 0.0f;
                vBuffer[line][pixel] = 0.0f;
            }
        }
    }

    if (!colorBurstDetected) {
        qWarning() << "No color burst detected in any line. Treating image as grayscale.";
    }
}

QImage PALBDemodulator::createImage()
{
    QImage image(PIXELS_PER_LINE, VISIBLE_LINES, QImage::Format_RGB888);

    for (int y = 0; y < VISIBLE_LINES; ++y) {
        for (int x = 0; x < PIXELS_PER_LINE; ++x) {
            uint8_t r, g, b;
            int actualY = y + (LINES_PER_FRAME - VISIBLE_LINES) / 2;
            yuv2rgb(frameBuffer[actualY][x],
                    uBuffer[actualY][x],
                    vBuffer[actualY][x],
                    r, g, b);
            image.setPixel(x, y, qRgb(r, g, b));
        }
    }
    return image;
}

void PALBDemodulator::designFilters()
{
    // Adjust the cutoff frequencies
    double videoLowCutoff = (VIDEO_CARRIER - 2.5e6) / sampleRate;
    double videoHighCutoff = (VIDEO_CARRIER + 2.5e6) / sampleRate;
    double audioLowCutoff = (AUDIO_CARRIER - 15e3) / sampleRate;
    double audioHighCutoff = (AUDIO_CARRIER + 15e3) / sampleRate;
    double colorLowCutoff = (COLOR_SUBCARRIER - 0.5e6) / sampleRate;
    double colorHighCutoff = (COLOR_SUBCARRIER + 0.5e6) / sampleRate;

    auto designBandPass = [this](double lowCutoff, double highCutoff, int length) {
        std::vector<float> filter(length);
        for (int i = 0; i < length; ++i) {
            double x = i - (length - 1) / 2.0;
            if (x == 0) {
                filter[i] = 2 * (highCutoff - lowCutoff);
            } else {
                filter[i] = (std::sin(2 * M_PI * highCutoff * x) - std::sin(2 * M_PI * lowCutoff * x)) / (M_PI * x);
            }
            // Apply Hamming window
            filter[i] *= 0.54 - 0.46 * std::cos(2 * M_PI * i / (length - 1));
        }
        // Normalize
        float sum = std::accumulate(filter.begin(), filter.end(), 0.0f);
        for (float& f : filter) f /= sum;
        return filter;
    };

    videoFilter = designBandPass(videoLowCutoff, videoHighCutoff, 64);
    audioFilter = designBandPass(audioLowCutoff, audioHighCutoff, 64);
    colorBandpassFilter = designBandPass(colorLowCutoff, colorHighCutoff, 64);
}

float PALBDemodulator::applyFilter(const std::vector<float>& filter, const std::vector<float>& signal, int index)
{
    float result = 0.0f;
    for (size_t i = 0; i < filter.size(); ++i) {
        if (index >= i) {
            result += filter[i] * signal[index - i];
        }
    }
    return result;
}

std::complex<float> PALBDemodulator::applyComplexFilter(const std::vector<float>& filter, const std::vector<std::complex<float>>& signal, int index)
{
    std::complex<float> result(0.0f, 0.0f);
    for (size_t i = 0; i < filter.size(); ++i) {
        if (index >= i) {
            result += filter[i] * signal[index - i];
        }
    }
    return result;
}

void PALBDemodulator::yuv2rgb(float y, float u, float v, uint8_t& r, uint8_t& g, uint8_t& b)
{
    // YUV to RGB conversion
    float rf = y + 1.140f * v;
    float gf = y - 0.395f * u - 0.581f * v;
    float bf = y + 2.032f * u;

    r = static_cast<uint8_t>(std::clamp(rf * 255.0f, 0.0f, 255.0f));
    g = static_cast<uint8_t>(std::clamp(gf * 255.0f, 0.0f, 255.0f));
    b = static_cast<uint8_t>(std::clamp(bf * 255.0f, 0.0f, 255.0f));
}
