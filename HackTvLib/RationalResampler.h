#ifndef RATIONALRESAMPLER_H
#define RATIONALRESAMPLER_H
#include <vector>
#include <cmath>
#include <complex>
#include <stdexcept>

class RationalResampler {
public:
    RationalResampler(unsigned interpolation, unsigned decimation, float filter_size)
        : interpolation(interpolation), decimation(decimation), filter_size(filter_size) {
        if (interpolation == 0 || decimation == 0) {
            throw std::out_of_range("Interpolation and decimation factors must be greater than zero");
        }
    }

    std::vector<std::complex<float>> resample(const std::vector<std::complex<float>>& input) {

        std::vector<std::complex<float>> filtered_input = apply_low_pass_filter(input);

        std::vector<std::complex<float>> interpolated_output;
        interpolated_output.reserve(input.size() * interpolation);

        for (size_t i = 0; i < filtered_input.size() - 1; ++i) {
            interpolated_output.push_back(filtered_input[i]);
            for (unsigned j = 1; j < interpolation; ++j) {
                std::complex<float> interpolated_sample;
                float t = static_cast<float>(j) / static_cast<float>(interpolation);
                interpolated_sample.real((1.0f - t) * filtered_input[i].real() + t * filtered_input[i + 1].real());
                interpolated_sample.imag((1.0f - t) * filtered_input[i].imag() + t * filtered_input[i + 1].imag());
                interpolated_output.push_back(interpolated_sample);
            }
        }
        interpolated_output.push_back(filtered_input.back());

        std::vector<std::complex<float>> output;
        output.reserve(interpolated_output.size() / decimation);

        for (size_t i = 0; i < interpolated_output.size(); i += decimation) {
            output.push_back(interpolated_output[i]);
        }

        return output;
    }

private:
    unsigned interpolation;
    unsigned decimation;
    float filter_size;

    std::vector<std::complex<float>> apply_low_pass_filter(const std::vector<std::complex<float>>& input) {
        std::vector<float> filter;
        int num_taps = static_cast<int>(7 * filter_size);
        float sum = 0.0f;

        for (int i = 0; i < num_taps; ++i) {
            float tap_value = std::exp(-0.5f * std::pow(i - (num_taps - 1) / 2.0f, 2) / (2 * std::pow(filter_size, 2)));
            filter.push_back(tap_value);
            sum += tap_value;
        }

        // Filtre normalizasyonu
        for (auto& tap : filter) {
            tap /= sum;
        }

        std::vector<std::complex<float>> filtered_input;

        // Filtreyi uygulama
        for (size_t i = 0; i < input.size(); ++i) {
            std::complex<float> sum = 0;
            for (size_t j = 0; j < filter.size(); ++j) {
                if (i >= j) {
                    sum += input[i - j] * filter[j];
                }
            }
            filtered_input.push_back(sum);
        }

        if(filter_size != 0)
            return filtered_input;
        else
            return input;
    }
};

#endif // RATIONALRESAMPLER_H
