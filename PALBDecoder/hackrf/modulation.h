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

// ============================================================
// FM Stereo MPX (Multiplex) Generator
// Generates composite baseband directly at the target TX sample rate.
// Audio (44100 Hz stereo) is upsampled to txSampleRate internally.
// This eliminates the need for a separate resampler stage.
// ============================================================
class StereoMPXGenerator {
public:
    StereoMPXGenerator(float audioSampleRate, float txSampleRate, float preEmphasisTau = 75e-6f)
        : m_audioSampleRate(audioSampleRate), m_txSampleRate(txSampleRate),
          m_pilotPhase(0.0f),
          m_prevL(0.0f), m_prevR(0.0f), m_prevOutL(0.0f), m_prevOutR(0.0f),
          m_lastL(0.0f), m_lastR(0.0f)
    {
        float dt = 1.0f / audioSampleRate;
        m_preEmphAlpha = dt / (preEmphasisTau + dt);

        // Phase increment at TX sample rate
        m_pilotPhaseInc = 2.0f * F_PI * 19000.0f / txSampleRate;

        // Upsample ratio: txSampleRate / audioSampleRate
        m_upsampleRatio = txSampleRate / audioSampleRate;
    }

    // Input: interleaved stereo at 44100 Hz (L, R, L, R, ...)
    // Output: MPX composite at txSampleRate (e.g. 2 MHz) — ready for FM modulation
    std::vector<float> process(const float* stereoData, size_t stereoSampleCount)
    {
        size_t audioFrames = stereoSampleCount / 2;
        if (audioFrames == 0) return {};

        // Pre-emphasis at audio rate
        std::vector<float> peL(audioFrames), peR(audioFrames);
        for (size_t i = 0; i < audioFrames; i++) {
            peL[i] = applyPreEmphasis(stereoData[i * 2], m_prevL, m_prevOutL);
            peR[i] = applyPreEmphasis(stereoData[i * 2 + 1], m_prevR, m_prevOutR);
        }

        // Output at TX sample rate
        size_t outFrames = static_cast<size_t>(audioFrames * m_upsampleRatio) + 1;
        std::vector<float> mpx;
        mpx.reserve(outFrames);

        for (size_t i = 0; i < audioFrames; i++) {
            float curL = peL[i];
            float curR = peR[i];

            float startPos = i * m_upsampleRatio;
            float endPos = (i + 1) * m_upsampleRatio;
            int samplesThisFrame = static_cast<int>(endPos) - static_cast<int>(startPos);
            if (samplesThisFrame < 1) samplesThisFrame = 1;

            for (int j = 0; j < samplesThisFrame; j++) {
                float t = static_cast<float>(j) / static_cast<float>(samplesThisFrame);
                float L = m_lastL + (curL - m_lastL) * t;
                float R = m_lastR + (curR - m_lastR) * t;

                float sum  = (L + R) * 0.45f;
                float diff = (L - R) * 0.45f;
                float pilot = 0.075f * std::sin(m_pilotPhase);
                float subcarrier = std::sin(2.0f * m_pilotPhase);

                mpx.push_back(sum + pilot + diff * subcarrier);

                m_pilotPhase += m_pilotPhaseInc;
                if (m_pilotPhase >= 2.0f * F_PI)
                    m_pilotPhase -= 2.0f * F_PI;
            }

            m_lastL = curL;
            m_lastR = curR;
        }

        return mpx;
    }

    float getOutputSampleRate() const { return m_txSampleRate; }

    void reset() {
        m_pilotPhase = 0.0f;
        m_prevL = m_prevR = m_prevOutL = m_prevOutR = 0.0f;
        m_lastL = m_lastR = 0.0f;
    }

private:
    float applyPreEmphasis(float in, float& prevIn, float& prevOut) {
        float out = in - (1.0f - m_preEmphAlpha) * prevIn;
        prevIn = in;
        prevOut = out;
        return out;
    }

    float m_audioSampleRate;
    float m_txSampleRate;
    float m_pilotPhase;
    float m_pilotPhaseInc;
    float m_preEmphAlpha;
    float m_upsampleRatio;

    float m_prevL, m_prevR;
    float m_prevOutL, m_prevOutR;
    float m_lastL, m_lastR;
};

class FrequencyModulator {
public:
    FrequencyModulator(float sensitivity, bool enablePreEmphasis = true)
        : d_sensitivity(sensitivity), d_phase(0.0f),
          alpha(enablePreEmphasis ? 0.75f : 0.0f), prev(0.0f) {}

    int work(int noutput_items, const std::vector<float>& input_items, std::vector<std::complex<float>>& output_items) {
        for (int i = 0; i < noutput_items; ++i) {
            float in = input_items[i];
            float processed = in - alpha * prev;
            prev = in;
            d_phase += d_sensitivity * processed;
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
