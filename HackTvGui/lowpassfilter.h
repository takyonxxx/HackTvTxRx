#ifndef LOWPASSFILTER_H
#define LOWPASSFILTER_H
#include <vector>
#include <complex>

class LowPassFilter {
public:
    LowPassFilter(double sampleRate, double cutoffFreq, double transitionWidth, double decimation);
    std::vector<std::complex<float>> apply(const std::vector<std::complex<float>>& input);

private:
    std::vector<float> taps;
    int decimation;
    void designFilter(double sampleRate, double cutoffFreq, double transitionWidth);
};

#endif // LOWPASSFILTER_H
