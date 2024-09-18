#ifndef FMDEMODULATOR_H
#define FMDEMODULATOR_H

#include <QObject>
#include <complex>
#include <vector>

class FMDemodulator : public QObject
{
    Q_OBJECT

public:
    explicit FMDemodulator(double quadratureRate, int audioDecimation, QObject *parent = nullptr);
    std::vector<float> demodulate(const std::vector<std::complex<float>>& samples);

private:
    double quadratureRate;
    int audioDecimation;
    std::complex<float> lastSample;
    float removeDCOffset(const std::vector<float>& input);
    std::vector<float> applyLowPassFilter(const std::vector<float>& input);
    float softClip(float x);
};

#endif // FMDEMODULATOR_H
