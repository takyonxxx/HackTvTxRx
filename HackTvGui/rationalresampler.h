#ifndef RATIONALRESAMPLER_H
#define RATIONALRESAMPLER_H

#include <vector>
#include <complex>

class RationalResampler {
public:
    RationalResampler(int interpolation, int decimation);
    std::vector<std::complex<float>> resample(const std::vector<std::complex<float>>& input);

private:
    int interpolation;
    int decimation;
    std::vector<float> filter;
    void designFilter();
    std::complex<float> safeComplex(float real, float imag);
};

#endif // RATIONALRESAMPLER_H
