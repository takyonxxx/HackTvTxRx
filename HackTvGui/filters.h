#ifndef FILTERS_H
#define FILTERS_H

#include <vector>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <stdexcept>

#define _GHZ(x) ((uint64_t)(x) * 1000000000)
#define _MHZ(x) ((x) * 1000000)
#define _KHZ(x) ((x) * 1000)
#define _HZ(x) ((x) * 1)

#define DEFAULT_FREQUENCY              _MHZ(100)
#define DEFAULT_RF_SAMPLE_RATE         _MHZ(16)
#define DEFAULT_AUDIO_SAMPLE_RATE      _KHZ(48)
#define DEFAULT_CUT_OFF                _KHZ(75)

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


// Hamming window function
std::vector<float> hamming_window(int n) {
    std::vector<float> window(n);
    for (int i = 0; i < n; ++i) {
        window[i] = 0.54f - 0.46f * std::cos(2 * M_PI * i / (n - 1));
    }
    return window;
}

// Design a low-pass FIR filter
std::vector<float> design_low_pass_filter(int taps, float sample_rate, float cutoff_freq, float transition_width) {
    std::vector<float> filter_taps(taps);

    float fc = cutoff_freq / (sample_rate / 2);  // Normalized cutoff frequency (0 to 1)
    float tw = transition_width / (sample_rate / 2);  // Normalized transition width

    int m = taps - 1;
    float sum = 0.0f;

    // Generate the ideal sinc filter
    for (int i = 0; i <= m; ++i) {
        float sinc_arg = M_PI * (i - m / 2.0f);
        if (sinc_arg == 0.0f) {
            filter_taps[i] = 2 * fc;
        } else {
            filter_taps[i] = std::sin(2 * M_PI * fc * (i - m / 2.0f)) / sinc_arg;
        }

        // Apply the Hamming window
        filter_taps[i] *= 0.54f - 0.46f * std::cos(2 * M_PI * i / m);

        sum += filter_taps[i];
    }

    // Normalize filter taps
    for (int i = 0; i <= m; ++i) {
        filter_taps[i] /= sum;
    }

    return filter_taps;
}


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

class RationalResampler {
public:
    RationalResampler(unsigned interpolation, unsigned decimation, const std::vector<float>& taps = {}, float fractional_bw = 0.4)
        : interpolation_(interpolation), decimation_(decimation), fractional_bw_(fractional_bw), taps_(taps) {
        if (interpolation == 0 || decimation == 0) {
            throw std::out_of_range("Interpolation and decimation factors must be > 0");
        }

        if (taps_.empty()) {
            if (fractional_bw <= 0 || fractional_bw >= 0.5) {
                throw std::range_error("Invalid fractional_bandwidth, must be in (0, 0.5)");
            }
            taps_ = design_resampler_filter(interpolation_, decimation_, fractional_bw_);
        }

        gcd_ = std::gcd(interpolation_, decimation_);
        decimation_ /= gcd_;
        interpolation_ /= gcd_;

        prepare_filters();
    }

    std::vector<std::complex<float>> resample(const std::vector<std::complex<float>>& input) {
        std::vector<std::complex<float>> output;
        size_t input_size = input.size();
        size_t output_size = (input_size * interpolation_) / decimation_;
        output.resize(output_size);

        unsigned int ctr = 0;
        int count = 0;

        for (size_t i = 0; i < output_size; ++i) {
            output[i] = filter(input.data() + count, ctr);
            ctr += decimation_;
            if (ctr >= interpolation_) {
                ctr -= interpolation_;
                ++count;
            }
        }

        return output;
    }

private:
    unsigned interpolation_;
    unsigned decimation_;
    unsigned gcd_;
    float fractional_bw_;
    std::vector<float> taps_;
    std::vector<std::vector<float>> firs_;

    std::vector<float> design_resampler_filter(unsigned interpolation, unsigned decimation, float fractional_bw) {
        // Design the low-pass filter taps using a Kaiser window
        // This is a simplified placeholder. You might want to replace this with your actual filter design.
        unsigned num_taps = interpolation > decimation ? interpolation : decimation;
        std::vector<float> taps(num_taps, 1.0f);  // Simplified placeholder for actual filter design
        return taps;
    }

    void prepare_filters() {
        size_t num_filters = interpolation_;
        size_t tap_size = taps_.size() / num_filters;
        firs_.resize(num_filters);

        for (unsigned i = 0; i < num_filters; ++i) {
            firs_[i].resize(tap_size);
            for (size_t j = 0; j < tap_size; ++j) {
                firs_[i][j] = taps_[i + j * num_filters];
            }
        }
    }

    std::complex<float> filter(const std::complex<float>* input, unsigned int ctr) {
        const auto& fir = firs_[ctr];
        std::complex<float> output = 0;

        for (size_t i = 0; i < fir.size(); ++i) {
            output += input[i] * fir[i];
        }

        return output;
    }
};

class WfmDemodulator {
public:
    WfmDemodulator(float gain)
        : d_gain(gain)
    {
        // Initialize history buffer to store the previous sample
        d_history.resize(2, std::complex<float>(0.0, 0.0));
    }

    std::vector<float> demodulate(const std::vector<std::complex<float>>& input) {
        std::vector<float> output(input.size());

        for (size_t i = 1; i < input.size(); ++i) {
            std::complex<float> product = input[i] * std::conj(d_history[0]);
            output[i] = d_gain * std::atan2(product.imag(), product.real());
            d_history[0] = input[i];
        }

        return output;
    }

private:
    float d_gain;
    std::vector<std::complex<float>> d_history;
};

class FirFilter {
public:
    FirFilter(const std::vector<float>& taps, int decimation = 1) : d_decimation(decimation) {
        setTaps(taps);
    }

    void setTaps(const std::vector<float>& taps) {
        d_taps = taps;
        d_ntaps = d_taps.size();
        d_nsamples = d_ntaps;  // For this simple version, nsamples equals ntaps

        // Initialize the tail (overlap-save) buffer to zero
        d_tail.resize(d_ntaps - 1, 0.0f);
    }

    std::vector<float> filter(const std::vector<float>& input) {
        std::vector<float> output;

        // Buffer for storing the input and tail together
        std::vector<float> inbuf(d_nsamples + d_ntaps - 1);

        // Copy tail to the start of inbuf
        std::copy(d_tail.begin(), d_tail.end(), inbuf.begin());

        // Loop over input in blocks of d_nsamples
        for (size_t i = 0; i < input.size(); i += d_nsamples) {
            size_t block_size = std::min(d_nsamples, (int)(input.size() - i));

            // Copy the next block of input to inbuf after the tail
            std::copy(input.begin() + i, input.begin() + i + block_size, inbuf.begin() + d_ntaps - 1);

            // Apply the filter
            for (int n = 0; n < block_size; ++n) {
                float sum = 0.0f;
                for (int k = 0; k < d_ntaps; ++k) {
                    sum += inbuf[n + k] * d_taps[d_ntaps - k - 1];
                }
                output.push_back(sum);
            }

            // Update the tail with the last part of inbuf
            std::copy(inbuf.end() - (d_ntaps - 1), inbuf.end(), d_tail.begin());
        }

        return output;
    }

private:
    std::vector<float> d_taps;   // Filter coefficients
    int d_decimation;            // Decimation factor
    std::vector<float> d_tail;   // Overlap-save buffer
    int d_ntaps;                 // Number of taps
    int d_nsamples;              // Number of samples to process in one go
};

#endif // FILTERS_H
