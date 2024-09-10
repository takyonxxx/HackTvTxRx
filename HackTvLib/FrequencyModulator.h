#ifndef FREQUENCYMODULATOR_H
#define FREQUENCYMODULATOR_H
#include <vector>
#include <cmath>
#include <complex>

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

#endif // FREQUENCYMODULATOR_H
