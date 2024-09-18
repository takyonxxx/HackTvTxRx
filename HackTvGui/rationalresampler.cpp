#include "rationalresampler.h"
#include "constants.h"

RationalResampler::RationalResampler(int interpolation, int decimation)
    : interpolation(interpolation), decimation(decimation)
{
    designFilter();
}

void RationalResampler::designFilter()
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

std::complex<float> RationalResampler::safeComplex(float real, float imag) {
    real = std::isfinite(real) ? std::clamp(real, -1.0f, 1.0f) : 0.0f;
    imag = std::isfinite(imag) ? std::clamp(imag, -1.0f, 1.0f) : 0.0f;
    return std::complex<float>(real, imag);
}

std::vector<std::complex<float>> RationalResampler::resample(const std::vector<std::complex<float>>& input)
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
