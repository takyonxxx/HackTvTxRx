#include "fmdemodulator.h"
#include "constants.h"

FMDemodulator::FMDemodulator(double quadratureRate, int audioDecimation, QObject *parent)
    : QObject(parent), quadratureRate(quadratureRate), audioDecimation(audioDecimation), lastSample(1, 0)
{
}
std::vector<float> FMDemodulator::demodulate(const std::vector<std::complex<float>>& samples)
{
    std::vector<float> demodulated;
    demodulated.reserve(samples.size() / audioDecimation);

    std::complex<float> prevSample = samples[0];
    for (size_t i = 1; i < samples.size(); i++) {
        std::complex<float> product = samples[i] * std::conj(prevSample);
        float phase = std::arg(product);

        if (std::isnan(phase) || std::isinf(phase)) {
            qDebug() << "FMDemodulator: Invalid phase at sample" << i;
            phase = 0.0f;
        }

        if (i % audioDecimation == 0) {
            float demodValue = phase * (quadratureRate / (2 * M_PI * 75e3)); // 75 kHz max deviation for WBFM
            demodValue = softClip(demodValue);
            demodulated.push_back(demodValue);
        }
        prevSample = samples[i];
    }

    // Remove DC offset
    float dcOffset = removeDCOffset(demodulated);
    for (float& sample : demodulated) {
        sample -= dcOffset;
    }

    // Apply low-pass filter
    demodulated = applyLowPassFilter(demodulated);
    return demodulated;
}

float FMDemodulator::removeDCOffset(const std::vector<float>& input)
{
    float sum = std::accumulate(input.begin(), input.end(), 0.0f);
    return sum / input.size();
}

std::vector<float> FMDemodulator::applyLowPassFilter(const std::vector<float>& input)
{
    std::vector<float> output(input.size());
    const float alpha = 0.4f; // Adjust this value to change the cutoff frequency

    output[0] = input[0];
    for (size_t i = 1; i < input.size(); ++i) {
        output[i] = output[i-1] + alpha * (input[i] - output[i-1]);
    }

    return output;
}

float FMDemodulator::softClip(float x)
{
    const float threshold = 0.8f;
    if (x > threshold)
        return threshold + std::tanh(x - threshold);
    else if (x < -threshold)
        return -threshold - std::tanh(-x - threshold);
    return x;
}
