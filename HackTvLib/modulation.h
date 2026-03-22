#ifndef MODULATION_H
#define MODULATION_H

#include <vector>
#include <cmath>
#include <complex>
#include <stdexcept>

#define M_PI 3.14159265358979323846
#define F_PI ((float)(M_PI))

namespace fxpt
{
constexpr int32_t FIXED_POINT_ONE = 1 << 16;
inline int32_t float_to_fixed(float x) {
    return static_cast<int32_t>(x * FIXED_POINT_ONE);
}
inline void sincos(int32_t angle, float* sin_out, float* cos_out) {
    float radians = static_cast<float>(angle) / FIXED_POINT_ONE;
    *sin_out = std::sin(radians);
    *cos_out = std::cos(radians);
}
}

const double FREQUENCY = 440;        // Frequency of the sine wave in Hz
const double MODULATION_INDEX = 0.5; // FM modulation index
const double MODULATION_FREQUENCY = 75000; // Frequency of modulation in Hz

inline void generate_fm_samples(int16_t* buffer, size_t num_samples, int sample_rate) {
    const double time_step = 1.0 / sample_rate;
    double t = 0.0;
    double phase = 0.0;

    // Precompute some constants
    const double omega_c = 2 * M_PI * FREQUENCY;
    const double omega_m = 2 * M_PI * MODULATION_FREQUENCY;
    const double beta = MODULATION_INDEX * MODULATION_FREQUENCY / MODULATION_FREQUENCY;

    for (size_t i = 0; i < num_samples; i += 2) {
        // Calculate the instantaneous phase
        double inst_phase = omega_c * t + beta * std::sin(omega_m * t);

        // Generate complex FM sample
        std::complex<double> sample = std::exp(std::complex<double>(0, inst_phase));

        // Scale and convert to 16-bit integers for I and Q
        buffer[i] = static_cast<int16_t>(sample.real() * 32767);
        buffer[i+1] = static_cast<int16_t>(sample.imag() * 32767);

        t += time_step;
    }
}

class FrequencyModulator {
public:
    FrequencyModulator(float sensitivity)
        : d_sensitivity(sensitivity), d_phase(0.0f), alpha(0.75f), prev(0.0f) {}

    int work(int noutput_items, const std::vector<float>& input_items, std::vector<std::complex<float>>& output_items) {
        for (int i = 0; i < noutput_items; ++i) {
            // Apply pre-emphasis filter
            float in = input_items[i];
            float pre_emphasis = in - alpha * prev;
            prev = in;
            d_phase += d_sensitivity * pre_emphasis;
            // Place phase in [-pi, +pi[
            d_phase = std::fmod(d_phase + F_PI, 2.0f * F_PI) - F_PI;
            float oi, oq;
            int32_t angle = fxpt::float_to_fixed(d_phase);
            fxpt::sincos(angle, &oq, &oi);
            output_items[i] = std::complex<float>(oi, oq);
        }
        return noutput_items;
    }

private:
    float d_sensitivity;
    float d_phase;
    float alpha; // Pre-emphasis filter coefficient
    float prev;  // Previous input for the pre-emphasis filter
};

class RationalResampler {
public:
    RationalResampler(unsigned interpolation, unsigned decimation, float filter_size)
        : interpolation(interpolation), decimation(decimation), filter_size(filter_size),
          m_lastSample(0.0f, 0.0f), m_hasHistory(false) {
        if (interpolation == 0 || decimation == 0) {
            throw std::out_of_range("Interpolation and decimation factors must be greater than zero");
        }
        // Pre-compute filter taps once (they never change)
        if (filter_size > 0.0f) {
            int num_taps = static_cast<int>(7 * filter_size);
            if (num_taps > 0) {
                m_taps.resize(num_taps);
                float sum = 0.0f;
                for (int i = 0; i < num_taps; ++i) {
                    float tap_value = std::exp(-0.5f * std::pow(i - (num_taps - 1) / 2.0f, 2) / (2 * std::pow(filter_size, 2)));
                    m_taps[i] = tap_value;
                    sum += tap_value;
                }
                for (auto& tap : m_taps) {
                    tap /= sum;
                }
                // Initialize filter history buffer (zeros)
                m_filterHistory.resize(num_taps, std::complex<float>(0.0f, 0.0f));
            }
        }
    }

    std::vector<std::complex<float>> resample(const std::vector<std::complex<float>>& input) {
        if (input.empty()) return {};

        // Apply low-pass filter with persistent history
        std::vector<std::complex<float>> filtered_input = apply_low_pass_filter(input);

        if (filtered_input.empty()) return {};

        // Interpolation with inter-callback continuity
        std::vector<std::complex<float>> interpolated_output;
        interpolated_output.reserve(filtered_input.size() * interpolation);

        // Use last sample from previous callback for first interpolation span
        std::complex<float> prev = m_hasHistory ? m_lastSample : filtered_input[0];

        for (size_t i = 0; i < filtered_input.size(); ++i) {
            std::complex<float> curr = filtered_input[i];

            // Interpolate from prev to curr
            for (unsigned j = 0; j < interpolation; ++j) {
                float t = static_cast<float>(j) / static_cast<float>(interpolation);
                std::complex<float> interp_sample;
                interp_sample.real((1.0f - t) * prev.real() + t * curr.real());
                interp_sample.imag((1.0f - t) * prev.imag() + t * curr.imag());
                interpolated_output.push_back(interp_sample);
            }

            prev = curr;
        }

        // Save last sample for next callback
        m_lastSample = filtered_input.back();
        m_hasHistory = true;

        // Decimation
        std::vector<std::complex<float>> output;
        output.reserve(interpolated_output.size() / decimation + 1);

        for (size_t i = 0; i < interpolated_output.size(); i += decimation) {
            output.push_back(interpolated_output[i]);
        }

        return output;
    }

private:
    unsigned interpolation;
    unsigned decimation;
    float filter_size;

    // Persistent state across calls
    std::vector<float> m_taps;                          // Pre-computed filter coefficients
    std::vector<std::complex<float>> m_filterHistory;   // Filter delay line (past samples)
    std::complex<float> m_lastSample;                   // Last output for interpolation continuity
    bool m_hasHistory;

    std::vector<std::complex<float>> apply_low_pass_filter(const std::vector<std::complex<float>>& input) {
        if (m_taps.empty() || filter_size == 0.0f) {
            return input;
        }

        const size_t num_taps = m_taps.size();
        std::vector<std::complex<float>> filtered_input;
        filtered_input.reserve(input.size());

        for (size_t i = 0; i < input.size(); ++i) {
            // Shift history: remove oldest, add newest
            m_filterHistory.erase(m_filterHistory.begin());
            m_filterHistory.push_back(input[i]);

            // Convolve with filter taps
            std::complex<float> acc(0.0f, 0.0f);
            for (size_t j = 0; j < num_taps; ++j) {
                acc += m_filterHistory[j] * m_taps[num_taps - 1 - j];
            }
            filtered_input.push_back(acc);
        }

        return filtered_input;
    }
};

inline std::vector<std::complex<float>> apply_modulation(std::vector<float> buffer)
{
    int decimation = 1;
    int interpolation = 32;
    float sensitivity = 1.0;
    float filter_size = 0.0;
    float amplitude = 1.0;

    size_t desired_size = buffer.size() / 2;
    std::vector<float> float_buffer(buffer.begin(), buffer.begin() + desired_size);

    // Apply amplitude scaling
    for (auto& sample : float_buffer) {
        sample *= amplitude;
    }

    FrequencyModulator modulator(sensitivity);
    std::vector<std::complex<float>> modulated_signal(float_buffer.size());
    modulator.work(float_buffer.size(), float_buffer, modulated_signal);

    RationalResampler resampler(interpolation, decimation, filter_size);
    std::vector<std::complex<float>> resampled_signal = resampler.resample(modulated_signal);

    return resampled_signal;
}


#endif // MODULATION_H
