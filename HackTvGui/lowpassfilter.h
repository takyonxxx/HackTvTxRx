#ifndef LOWPASSFILTER_H
#define LOWPASSFILTER_H
#include <vector>
#include <complex>

class LowPassFilter {
public:
    LowPassFilter(double sampleRate, double cutoffFreq, double transitionWidth);
    std::vector<std::complex<float>> apply(const std::vector<std::complex<float>>& input);
    void designFilter(double sampleRate, double cutoffFreq, double transitionWidth);
    int calculateDecimation(double sampleRate);

private:
    std::vector<float> taps;
    int decimation;
};

#endif // LOWPASSFILTER_H
