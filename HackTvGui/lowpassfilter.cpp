#include "lowpassfilter.h"
#include <cmath>
#include <algorithm>
#include "constants.h"

LowPassFilter::LowPassFilter(double sampleRate, double cutoffFreq, double transitionWidth)
{
    designFilter(sampleRate, cutoffFreq, transitionWidth);
}

void LowPassFilter::designFilter(double sampleRate, double cutoffFreq, double transitionWidth)
{
    decimation = calculateDecimation(sampleRate);

    int numTaps = static_cast<int>(3.3 / (transitionWidth / sampleRate));
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

std::vector<std::complex<float>> LowPassFilter::apply(const std::vector<std::complex<float>>& input)
{
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
            qDebug() << "LowPassFilter: NaN detected at output sample" << output.size() - 1
                     << "Input samples:";
            for (size_t k = i; k < std::min(i + taps.size(), input.size()); ++k) {
                qDebug() << "  " << k << ":" << input[k];
            }
        }
    }

    return output;
}

int LowPassFilter::calculateDecimation(double sampleRate)
{
    constexpr std::array<std::pair<double, int>, 7> sampleRateToDecimation = {{
        {2e6, 7},
        {4e6, 14},
        {8e6, 28},
        {10e6, 35},
        {12.5e6, 44},
        {16e6, 56},
        {20e6, 70}
    }};

    // Find the closest sample rate in our predefined list
    auto it = std::lower_bound(sampleRateToDecimation.begin(), sampleRateToDecimation.end(),
                               std::pair<double, int>(sampleRate, 0),
                               [](const auto& a, const auto& b) { return a.first < b.first; });

    if (it == sampleRateToDecimation.end()) {
        // If sample rate is higher than our highest predefined value, use the highest decimation
        return sampleRateToDecimation.back().second;
    }

    if (it == sampleRateToDecimation.begin() || it->first == sampleRate) {
        // Exact match or lower than our lowest predefined value
        return it->second;
    }

    // Interpolate between the two closest values
    auto prev = std::prev(it);
    double t = (sampleRate - prev->first) / (it->first - prev->first);
    return static_cast<int>(std::round(prev->second * (1 - t) + it->second * t));
}
