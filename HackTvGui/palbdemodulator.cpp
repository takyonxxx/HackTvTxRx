#include "palbdemodulator.h"
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <numeric>

PALBDemodulator::PALBDemodulator(double sampleRate, QObject *parent)
    : QObject(parent), sampleRate(sampleRate), colorBurstPhase(0.0f),
    frequencyOffset(0.0), agcGain(1.0f)
{
    if (sampleRate != 20e6) {
        qWarning() << "Sample rate is not 20 MHz. Adjusting processing accordingly.";
    }

    designFilters();
    colorBurstOscillator = std::complex<float>(1.0f, 0.0f);

    // Initialize buffers
    frameBuffer.fill({});
    uBuffer.fill({});
    vBuffer.fill({});
    agcBuffer.resize(1024, 1.0f);  // Initialize AGC buffer
}

PALBDemodulator::DemodulatedFrame PALBDemodulator::demodulate(const std::vector<std::complex<float>>& samples)
{
    DemodulatedFrame frame;

    // Step 1: Estimate and correct frequency offset
    estimateFrequencyOffset();
    std::vector<std::complex<float>> correctedSamples = samples;
    applyFrequencyCorrection(correctedSamples);

    // Step 2: Demodulate the AM signal
    demodulateAM(correctedSamples);

    // Step 3: Apply AGC
    applyAGC(videoSignal);

    // Step 4: Extract video and audio signals
    extractVideoSignal();
    extractAudioSignal();

    // Step 5: Process video signal
    synchronizeHorizontal();
    synchronizeVertical();
    decodeColor();

    // Step 6: Create QImage from processed video data
    frame.image = createImage();

    // Step 7: Process audio signal (simplified)
    frame.audio = audioSignal;

    return frame;
}

void PALBDemodulator::estimateFrequencyOffset()
{
    // Implement a frequency offset estimation algorithm
    // This is a placeholder and should be replaced with a proper algorithm
    // For example, you could use a FFT-based method to detect the carrier frequency
    frequencyOffset = 0.0;  // Placeholder
}

void PALBDemodulator::applyFrequencyCorrection(std::vector<std::complex<float>>& samples)
{
    for (size_t i = 0; i < samples.size(); ++i) {
        float phase = 2 * M_PI * frequencyOffset * i / sampleRate;
        std::complex<float> correction(std::cos(phase), -std::sin(phase));
        samples[i] *= correction;
    }
}

void PALBDemodulator::applyAGC(std::vector<float>& signal)
{
    const float targetLevel = 0.5f;
    const float attackRate = 0.01f;
    const float decayRate = 0.001f;

    for (float& sample : signal) {
        float absample = std::abs(sample);
        agcBuffer.pop_front();
        agcBuffer.push_back(absample);

        float maxLevel = *std::max_element(agcBuffer.begin(), agcBuffer.end());

        if (maxLevel > targetLevel * agcGain) {
            agcGain -= attackRate * (maxLevel - targetLevel * agcGain);
        } else {
            agcGain += decayRate * (targetLevel * agcGain - maxLevel);
        }

        sample *= agcGain;
    }
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
    audioSignal.resize(videoSignal.size());
    for (size_t i = 0; i < videoSignal.size(); ++i) {
        audioSignal[i] = applyFilter(audioFilter, videoSignal, i);
    }
}

void PALBDemodulator::synchronizeHorizontal()
{
    const int SYNC_THRESHOLD = 100;  // Adjust based on your signal characteristics
    const int SAMPLES_PER_LINE = static_cast<int>(sampleRate * LINE_DURATION);

    int lineStart = 0;
    for (int line = 0; line < LINES_PER_FRAME; ++line) {
        // Find sync pulse
        while (lineStart < videoSignal.size() - SAMPLES_PER_LINE &&
               videoSignal[lineStart] > SYNC_THRESHOLD) {
            ++lineStart;
        }

        // Process line
        if (lineStart + SAMPLES_PER_LINE <= videoSignal.size()) {
            for (int pixel = 0; pixel < PIXELS_PER_LINE; ++pixel) {
                int sampleIndex = lineStart + pixel * SAMPLES_PER_LINE / PIXELS_PER_LINE;
                frameBuffer[line][pixel] = videoSignal[sampleIndex];
            }
        }

        lineStart += SAMPLES_PER_LINE;
    }
}

void PALBDemodulator::synchronizeVertical()
{
    const int FIELD_SYNC_LINES = 5;
    const int FIELD_SYNC_THRESHOLD = 50;  // Adjust based on your signal characteristics

    int fieldStart = 0;
    for (int i = 0; i < LINES_PER_FRAME - FIELD_SYNC_LINES; ++i) {
        int syncCount = 0;
        for (int j = 0; j < FIELD_SYNC_LINES; ++j) {
            if (*std::min_element(frameBuffer[i+j].begin(), frameBuffer[i+j].end()) < FIELD_SYNC_THRESHOLD) {
                ++syncCount;
            }
        }
        if (syncCount >= FIELD_SYNC_LINES - 1) {
            fieldStart = i + FIELD_SYNC_LINES;
            break;
        }
    }

    // Adjust frame buffer to start at the beginning of a field
    std::rotate(frameBuffer.begin(), frameBuffer.begin() + fieldStart, frameBuffer.end());
}

void PALBDemodulator::decodeColor()
{
    const float BURST_START = 5.6e-6f;  // 5.6 µs after the start of horizontal sync
    const float BURST_DURATION = 2.25e-6f;  // 2.25 µs duration
    const int BURST_START_SAMPLE = static_cast<int>(BURST_START * sampleRate);
    const int BURST_END_SAMPLE = static_cast<int>((BURST_START + BURST_DURATION) * sampleRate);

    for (int line = 0; line < LINES_PER_FRAME; ++line) {
        // Extract color burst
        std::complex<float> burst(0.0f, 0.0f);
        for (int i = BURST_START_SAMPLE; i < BURST_END_SAMPLE; ++i) {
            float sample = frameBuffer[line][i];
            burst += std::complex<float>(sample * std::cos(colorBurstPhase), sample * std::sin(colorBurstPhase));
            colorBurstPhase += 2 * M_PI * COLOR_SUBCARRIER / sampleRate;
        }
        burst /= (BURST_END_SAMPLE - BURST_START_SAMPLE);

        // Correct color burst phase
        float burstPhase = std::arg(burst);
        std::complex<float> burstCorrection = std::exp(std::complex<float>(0, -burstPhase));

        // Demodulate color
        for (int pixel = 0; pixel < PIXELS_PER_LINE; ++pixel) {
            float y = frameBuffer[line][pixel];
            std::complex<float> color = applyComplexFilter(colorBandpassFilter,
                                                           reinterpret_cast<const std::vector<std::complex<float>>&>(frameBuffer[line]), pixel);
            color *= burstCorrection;

            uBuffer[line][pixel] = color.real();
            vBuffer[line][pixel] = (line % 2 == 0 ? 1.0f : -1.0f) * color.imag();  // PAL phase alternation
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
    // Adjust the cutoff frequencies for 20 MHz sample rate
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
            // Apply Blackman window for better sidelobe suppression
            filter[i] *= 0.42 - 0.5 * std::cos(2 * M_PI * i / (length - 1)) + 0.08 * std::cos(4 * M_PI * i / (length - 1));
        }
        // Normalize
        float sum = std::accumulate(filter.begin(), filter.end(), 0.0f);
        for (float& f : filter) f /= sum;
        return filter;
    };

    videoFilter = designBandPass(videoLowCutoff, videoHighCutoff, 128);  // Increased filter length
    audioFilter = designBandPass(audioLowCutoff, audioHighCutoff, 128);
    colorBandpassFilter = designBandPass(colorLowCutoff, colorHighCutoff, 128);
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
