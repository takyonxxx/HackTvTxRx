#include "palbdemodulator.h"
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <numeric>

PALBDemodulator::PALBDemodulator(double sampleRate, QObject *parent)
    : QObject(parent), sampleRate(sampleRate), colorBurstPhase(0.0f)
{
    designFilters();
    colorBurstOscillator = std::complex<float>(1.0f, 0.0f);
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
    // Apply video bandpass filter
    std::vector<float> filteredVideo(videoSignal.size());
    for (size_t i = 0; i < videoSignal.size(); ++i) {
        filteredVideo[i] = applyFilter(videoFilter, videoSignal, i);
    }
    videoSignal = std::move(filteredVideo);
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
    int samplesPerLine = static_cast<int>(LINE_DURATION * sampleRate);
    int currentLine = 0;

    for (size_t i = 0; i < videoSignal.size(); i += samplesPerLine) {
        // Find sync pulse
        int syncStart = i;
        for (int j = 0; j < samplesPerLine; ++j) {
            if (videoSignal[i + j] < 0.1f) {  // Assuming sync level is below 0.1
                syncStart = i + j;
                break;
            }
        }

        // Extract active video region
        int activeStart = syncStart + static_cast<int>(0.005 * sampleRate);  // 5µs after sync
        for (int pixel = 0; pixel < PIXELS_PER_LINE; ++pixel) {
            int sampleIndex = activeStart + (pixel * samplesPerLine / PIXELS_PER_LINE);
            if (sampleIndex < videoSignal.size()) {
                frameBuffer[currentLine][pixel] = videoSignal[sampleIndex];
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
    int samplesPerLine = static_cast<int>(LINE_DURATION * sampleRate);

    for (int line = 0; line < LINES_PER_FRAME; ++line) {
        // Extract color burst
        int burstStart = static_cast<int>(0.005 * sampleRate);  // 5µs after sync
        int burstEnd = static_cast<int>(0.0075 * sampleRate);   // 7.5µs after sync
        std::complex<float> burst(0.0f, 0.0f);
        for (int i = burstStart; i < burstEnd; ++i) {
            float sample = frameBuffer[line][i];
            burst += std::complex<float>(sample * std::cos(colorBurstPhase), sample * std::sin(colorBurstPhase));
            colorBurstPhase += 2 * M_PI * COLOR_SUBCARRIER / sampleRate;
        }
        burst /= (burstEnd - burstStart);

        // Synchronize color subcarrier
        colorBurstOscillator = std::conj(burst) / std::abs(burst);

        // Demodulate color
        for (int pixel = 0; pixel < PIXELS_PER_LINE; ++pixel) {
            float y = frameBuffer[line][pixel];
            std::complex<float> color = std::complex<float>(y, 0.0f) * colorBurstOscillator;
            uBuffer[line][pixel] = color.real();
            vBuffer[line][pixel] = color.imag() * (line % 2 == 0 ? 1.0f : -1.0f);  // PAL switch

            colorBurstOscillator *= std::polar(1.0f, static_cast<float>(2 * M_PI * COLOR_SUBCARRIER / sampleRate));
        }
    }
}

QImage PALBDemodulator::createImage()
{
    QImage image(PIXELS_PER_LINE, VISIBLE_LINES, QImage::Format_RGB888);

    for (int y = 0; y < VISIBLE_LINES; ++y) {
        for (int x = 0; x < PIXELS_PER_LINE; ++x) {
            uint8_t r, g, b;
            yuv2rgb(frameBuffer[y + (LINES_PER_FRAME - VISIBLE_LINES) / 2][x],
                    uBuffer[y + (LINES_PER_FRAME - VISIBLE_LINES) / 2][x],
                    vBuffer[y + (LINES_PER_FRAME - VISIBLE_LINES) / 2][x],
                    r, g, b);
            image.setPixel(x, y, qRgb(r, g, b));
        }
    }

    return image;
}

void PALBDemodulator::designFilters()
{
    // Simplified filter design using windowed-sinc
    auto designLowPass = [this](double cutoff, int length) {
        std::vector<float> filter(length);
        for (int i = 0; i < length; ++i) {
            double x = i - (length - 1) / 2.0;
            if (x == 0) {
                filter[i] = 2 * M_PI * cutoff;
            } else {
                filter[i] = std::sin(2 * M_PI * cutoff * x) / x;
            }
            // Apply Hamming window
            filter[i] *= 0.54 - 0.46 * std::cos(2 * M_PI * i / (length - 1));
        }
        // Normalize
        float sum = std::accumulate(filter.begin(), filter.end(), 0.0f);
        for (float& f : filter) f /= sum;
        return filter;
    };

    videoFilter = designLowPass(VIDEO_CARRIER / sampleRate, 64);
    audioFilter = designLowPass(AUDIO_CARRIER / sampleRate, 64);
    colorBandpassFilter = designLowPass(COLOR_SUBCARRIER / sampleRate, 64);
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
