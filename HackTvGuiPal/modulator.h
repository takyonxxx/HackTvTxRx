#ifndef MODULATOR_H
#define MODULATOR_H

#include <QObject>
#include <QDebug>
#include <complex>
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>
#include <array>

class FMDemodulator : public QObject
{
    Q_OBJECT

public:
    explicit FMDemodulator(double quadratureRate, int audioDecimation, QObject *parent = nullptr)
        : QObject(parent), quadratureRate(quadratureRate), audioDecimation(audioDecimation), lastSample(1, 0)
    {
    }

    std::vector<float> demodulate(const std::vector<std::complex<float>>& samples)
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

private:
    double quadratureRate;
    int audioDecimation;
    std::complex<float> lastSample;

    float removeDCOffset(const std::vector<float>& input)
    {
        float sum = std::accumulate(input.begin(), input.end(), 0.0f);
        return sum / input.size();
    }

    std::vector<float> applyLowPassFilter(const std::vector<float>& input)
    {
        std::vector<float> output(input.size());
        const float alpha = 0.4f; // Adjust this value to change the cutoff frequency
        output[0] = input[0];
        for (size_t i = 1; i < input.size(); ++i) {
            output[i] = output[i-1] + alpha * (input[i] - output[i-1]);
        }
        return output;
    }

    float softClip(float x)
    {
        const float threshold = 0.8f;
        if (x > threshold)
            return threshold + std::tanh(x - threshold);
        else if (x < -threshold)
            return -threshold - std::tanh(-x - threshold);
        return x;
    }
};


class RationalResampler {
public:
    RationalResampler(int interpolation, int decimation)
        : interpolation(interpolation), decimation(decimation)
    {
        designFilter();
    }

    std::vector<std::complex<float>> resample(const std::vector<std::complex<float>>& input)
    {
        std::vector<std::complex<float>> output;
        output.reserve((input.size() * interpolation) / decimation);
        int inputIndex = 0;
        int outputIndex = 0;
        int64_t accumulator = 0;
        while (inputIndex < static_cast<int>(input.size())) {
            std::complex<float> sum(0, 0);
            for (size_t i = 0; i < filter.size(); ++i) {
                int sampleIndex = inputIndex - static_cast<int>(i) / interpolation;
                if (sampleIndex >= 0 && sampleIndex < static_cast<int>(input.size())) {
                    sum += input[sampleIndex] * filter[i];
                }
            }
            output.push_back(safeComplex(sum.real() * interpolation, sum.imag() * interpolation));
            accumulator += decimation;
            inputIndex += accumulator / interpolation;
            accumulator %= interpolation;
            outputIndex++;
        }
        return output;
    }

private:
    int interpolation;
    int decimation;
    std::vector<float> filter;

    void designFilter()
    {
        int numTaps = 64 * std::max(interpolation, decimation);
        filter.resize(numTaps);
        double cutoff = 0.5 * std::min(1.0 / interpolation, 1.0 / decimation);
        for (int n = 0; n < numTaps; n++) {
            double x = static_cast<double>(n - numTaps / 2) / interpolation;
            if (x == 0) {
                filter[n] = 2 * M_PI * cutoff;
            } else {
                filter[n] = std::sin(2 * M_PI * cutoff * x) / (M_PI * x);
            }
            // Apply Hamming window
            filter[n] *= 0.54 - 0.46 * std::cos(2 * M_PI * n / (numTaps - 1));
        }
        // Normalize for unity gain
        double sum = 0.0;
        for (float tap : filter) {
            sum += tap;
        }
        for (float& tap : filter) {
            tap /= sum;
        }
    }

    std::complex<float> safeComplex(float real, float imag) {
        real = std::isfinite(real) ? std::clamp(real, -1.0f, 1.0f) : 0.0f;
        imag = std::isfinite(imag) ? std::clamp(imag, -1.0f, 1.0f) : 0.0f;
        return std::complex<float>(real, imag);
    }
};

class LowPassFilter {
public:
    LowPassFilter(double sampleRate, double cutoffFreq, double transitionWidth) {
        designFilter(sampleRate, cutoffFreq, transitionWidth);
    }

    std::vector<std::complex<float>> apply(const std::vector<std::complex<float>>& input) {
        std::vector<std::complex<float>> output;
        output.reserve(input.size() / decimation);
        for (size_t i = 0; i < input.size(); i += decimation) {
            std::complex<float> sum(0, 0);
            for (size_t j = 0; j < taps.size(); j++) {
                if (i + j < input.size()) {
                    sum += input[i + j] * taps[j];
                }
            }
            // Bound the output values
            float real = std::clamp(sum.real(), -1.0f, 1.0f);
            float imag = std::clamp(sum.imag(), -1.0f, 1.0f);
            output.emplace_back(real, imag);
            if (std::isnan(real) || std::isnan(imag)) {
                qDebug() << "LowPassFilter: NaN detected at output sample" << output.size() - 1;
            }
        }
        return output;
    }

    void designFilter(double sampleRate, double cutoffFreq, double transitionWidth) {
        decimation = calculateDecimation(sampleRate);
        int numTaps = static_cast<int>(6.6 / (transitionWidth / sampleRate));
        numTaps = (numTaps % 2 == 0) ? numTaps + 1 : numTaps; // Ensure odd number of taps
        taps.resize(numTaps);
        double omega = 2 * M_PI * cutoffFreq / sampleRate;
        for (int n = 0; n < numTaps; n++) {
            int halfTaps = numTaps / 2;
            if (n == halfTaps) {
                taps[n] = omega / M_PI;
            } else {
                taps[n] = sin(omega * (n - halfTaps)) / (M_PI * (n - halfTaps));
            }
            // Apply Hamming window
            taps[n] *= (0.54 - 0.46 * cos(2 * M_PI * n / (numTaps - 1)));
        }
        // Normalize for unity gain
        double sum = 0.0;
        for (float tap : taps) {
            sum += tap;
        }
        for (float& tap : taps) {
            tap /= sum;
        }
    }

    int calculateDecimation(double sampleRate) {
        constexpr std::array<std::pair<double, int>, 7> sampleRateToDecimation = {{
            {2e6, 7},
            {4e6, 14},
            {8e6, 28},
            {10e6, 35},
            {12.5e6, 44},
            {16e6, 56},
            {20e6, 70}
        }};
        for (const auto& ratePair : sampleRateToDecimation) {
            if (sampleRate <= ratePair.first) {
                return ratePair.second;
            }
        }
        return 70; // Default decimation for higher sample rates
    }

private:
    int decimation;
    std::vector<float> taps;
};

#endif // MODULATOR_H


