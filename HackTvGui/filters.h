#ifndef FILTERS_H
#define FILTERS_H

#include <vector>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

#define M_PI 3.14159265358979323846
#define F_PI ((float)(M_PI))

namespace fxpt {
constexpr int32_t FIXED_POINT_ONE = 1 << 16;

int32_t float_to_fixed(float x) {
    return static_cast<int32_t>(x * FIXED_POINT_ONE);
}

void sincos(int32_t angle, float* sin_out, float* cos_out) {
    float radians = static_cast<float>(angle) / FIXED_POINT_ONE;
    *sin_out = std::sin(radians);
    *cos_out = std::cos(radians);
}
}


#define _GHZ(x) ((uint64_t)(x) * 1000000000)
#define _MHZ(x) ((x) * 1000000)
#define _KHZ(x) ((x) * 1000)
#define _HZ(x) ((x) * 1)

#define DEFAULT_FREQUENCY              _MHZ(100)
#define DEFAULT_RF_SAMPLE_RATE         _MHZ(16)
#define DEFAULT_AUDIO_SAMPLE_RATE      _KHZ(48)
#define DEFAULT_CUT_OFF                _KHZ(300)

class AGC {
public:
    AGC(float target_level = 0.5f, float attack_rate = 0.01f, float decay_rate = 0.001f)
        : target_level(target_level), attack_rate(attack_rate), decay_rate(decay_rate), gain(1.0f) {}

    float process(float input) {
        float amplitude = std::abs(input);
        if (amplitude > 1e-6f) {
            float error = target_level - amplitude * gain;
            gain *= (error > 0) ? (1.0f + attack_rate * error) : (1.0f + decay_rate * error);
            gain = std::clamp(gain, 0.1f, 10.0f);
        }
        return input * gain;
    }

private:
    float target_level, attack_rate, decay_rate, gain;
};

class FMDemodulator {
public:
    FMDemodulator(float max_freq_deviation, float sample_rate)
        : d_max_freq_deviation(max_freq_deviation),
        d_sample_rate(sample_rate),
        d_last_phase(0.0f) {}

    std::vector<float> demodulate(const std::vector<std::complex<float>>& input) {
        std::vector<float> output;
        output.reserve(input.size());

        for (const auto& sample : input) {
            float current_phase = std::arg(sample);
            float delta_phase = current_phase - d_last_phase;
            d_last_phase = current_phase;

            if (delta_phase > M_PI) delta_phase -= 2 * M_PI;
            if (delta_phase < -M_PI) delta_phase += 2 * M_PI;

            float freq = (delta_phase * d_sample_rate) / (2 * M_PI);
            float normalized_freq = freq / d_max_freq_deviation;
            normalized_freq = std::clamp(normalized_freq, -1.0f, 1.0f);

            output.push_back(normalized_freq);
        }

        return output;
    }

private:
    float d_max_freq_deviation;
    float d_sample_rate;
    float d_last_phase;
};

class FIRFilter {
public:
    FIRFilter(const std::vector<float>& coeffs) : coeffs(coeffs), buffer(coeffs.size()) {}

    float process(float input) {
        buffer.push_back(input);
        float output = 0.0f;
        for (size_t i = 0; i < coeffs.size(); ++i) {
            output += coeffs[i] * buffer[buffer.size() - 1 - i];
        }
        return output;
    }

private:
    std::vector<float> coeffs;
    std::vector<float> buffer;
};

class RationalResampler {
public:
    RationalResampler(int upsample_factor, int downsample_factor)
        : upsample_factor_(upsample_factor), downsample_factor_(downsample_factor), input_sample_count_(0) {
        if (upsample_factor <= 0 || downsample_factor <= 0) {
            throw std::invalid_argument("Upsample and downsample factors must be positive integers.");
        }
    }

    std::vector<float> process(float sample) {
        // Upsample by inserting the sample followed by (upsample_factor - 1) zeros
        std::vector<float> upsampled;
        upsampled.reserve(upsample_factor_);
        upsampled.push_back(sample);
        for (int i = 1; i < upsample_factor_; ++i) {
            upsampled.push_back(0.0f);
        }

        // Append upsampled samples to the buffer
        buffer_.insert(buffer_.end(), upsampled.begin(), upsampled.end());

        // Output downsampled samples
        std::vector<float> output;
        while (input_sample_count_ < buffer_.size()) {
            output.push_back(buffer_[input_sample_count_]);
            input_sample_count_ += downsample_factor_;
        }

        // Remove processed samples from the buffer
        if (input_sample_count_ >= buffer_.size()) {
            input_sample_count_ -= buffer_.size();
            buffer_.clear();
        } else {
            buffer_.erase(buffer_.begin(), buffer_.begin() + input_sample_count_);
            input_sample_count_ = 0;
        }

        return output;
    }

private:
    int upsample_factor_;
    int downsample_factor_;
    std::vector<float> buffer_;
    int input_sample_count_;
};

void resampleIQData(const std::vector<std::complex<float>>& iq_data,
                    std::vector<std::complex<float>>& resampled_iq_data,
                    double resample_ratio) {
    // Calculate the size of the resampled IQ data
    size_t resampled_size = static_cast<size_t>(iq_data.size() * resample_ratio);
    resampled_iq_data.resize(resampled_size);

    // Define the predecimation, interpolation, and decimation factors
    int predec = 0;
    int interp = 1;
    int decim = 70;

    // Calculate the input samples per output sample
    double samples_per_output = static_cast<double>(interp) / decim;

    // Initialize the index for the original IQ data
    double original_index = 0.0;

    // Resample the IQ data using the specified factors
    for (size_t i = 0; i < resampled_size; ++i) {
        // Calculate the integer and fractional parts of the original index
        size_t integer_index = static_cast<size_t>(original_index);
        double fraction = original_index - integer_index;

        // Ensure the integer index is within the bounds of the input data
        size_t lower_index = integer_index;
        size_t upper_index = std::min(integer_index + 1, iq_data.size() - 1);

        // Perform linear interpolation using lower and upper indices
        std::complex<float> interpolated_value = iq_data[lower_index] * std::complex<float>(1 - fraction, 0.0f) +
                                                 iq_data[upper_index] * std::complex<float>(fraction, 0.0f);

        // Store the resampled value in the output vector
        resampled_iq_data[i] = interpolated_value;

        // Increment the original index by the samples per output sample
        original_index += samples_per_output;
    }
}

void demodulateWFM(const std::vector<std::complex<float>>& resampled_iq_data,
                   std::vector<float>& demodulated_signal) {
    demodulated_signal.reserve(resampled_iq_data.size());
    for (size_t i = 1; i < resampled_iq_data.size(); ++i) {
        // Calculate the phase difference
        std::complex<float> previous_sample = resampled_iq_data[i - 1];
        std::complex<float> current_sample = resampled_iq_data[i];

        float phase_diff = std::arg(current_sample * std::conj(previous_sample));

        // Store the phase difference in the demodulated signal
        demodulated_signal.push_back(phase_diff);
    }
}

void applyLowPassFilter(const std::vector<float>& input_signal, std::vector<float>& output_signal,
                        int sample_rate, float cutoff_frequency, size_t num_taps = 101) {
    // Calculate the filter coefficients using the Hamming window method
    std::vector<float> filter_coefficients(num_taps);
    float nyquist = 0.5f * sample_rate;
    float normalized_cutoff = cutoff_frequency / nyquist;

    // Calculate the filter coefficients using the sinc function and Hamming window
    size_t mid = num_taps / 2;
    for (size_t n = 0; n < num_taps; ++n) {
        float hamming_window = 0.54 - 0.46 * cos((2 * M_PI * n) / (num_taps - 1));
        float sinc_value = sin(2 * M_PI * normalized_cutoff * (n - mid)) /
                           (M_PI * (n - mid));
        if (n == mid) {
            sinc_value = 2 * normalized_cutoff;
        }
        filter_coefficients[n] = hamming_window * sinc_value;
    }

    // Normalize the filter coefficients
    float sum = std::accumulate(filter_coefficients.begin(), filter_coefficients.end(), 0.0f);
    for (float& coeff : filter_coefficients) {
        coeff /= sum;
    }

    // Apply the filter to the input signal using convolution
    output_signal.resize(input_signal.size());
    for (size_t i = 0; i < input_signal.size(); ++i) {
        float filtered_sample = 0.0f;
        for (size_t j = 0; j < num_taps; ++j) {
            int index = static_cast<int>(i) - static_cast<int>(j) + static_cast<int>(mid);
            if (index >= 0 && index < static_cast<int>(input_signal.size())) {
                filtered_sample += input_signal[index] * filter_coefficients[j];
            }
        }
        output_signal[i] = filtered_sample;
    }
}

#endif // FILTERS_H
