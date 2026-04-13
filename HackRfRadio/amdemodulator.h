#ifndef AMDEMODULATOR_H
#define AMDEMODULATOR_H

#include <QObject>
#include <complex>
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class AMDemodulator : public QObject
{
    Q_OBJECT

public:
    explicit AMDemodulator(double inputSampleRate, double bandwidth = 10000.0, QObject *parent = nullptr);

    std::vector<float> demodulate(const std::vector<std::complex<float>>& samples);

    void setSampleRate(double newRate);
    void setBandwidth(double bandwidthHz);
    double bandwidth() const { return m_bandwidth; }

private:
    struct DecimStage {
        std::vector<float> taps;
        int factor;
        double outputRate;
        std::vector<std::complex<float>> iqHistory;
        std::vector<float> realHistory;
    };

    double m_inputRate;
    double m_bandwidth;

    std::vector<DecimStage> m_iqStages;
    std::vector<DecimStage> m_realStages;
    std::vector<float> m_audioFilterTaps;
    std::vector<float> m_iqBandwidthTaps;

    // Persistent filter state
    std::vector<std::complex<float>> m_iqBwHistory;
    std::vector<float> m_audioFilterHistory;

    // DC blocker state (SDR++ style)
    float m_dcOffset = 0.0f;

    // Proper DC blocker state (first-order HPF)
    float m_dcBlockerX = 0.0f;  // previous input
    float m_dcBlockerY = 0.0f;  // previous output

    // Running IQ DC removal
    float m_dcI = 0.0f;
    float m_dcQ = 0.0f;

    // AGC state
    float m_agcAmp = 0.0f;  // 0 = auto-init from first chunk

    // Output peak limiter
    float m_audioRms = 0.05f;  // running RMS estimate

    void rebuildChain();

    static std::vector<float> designLPF(int numTaps, float cutoff, float sampleRate);
    void decimateComplex(
        const std::vector<std::complex<float>>& in,
        std::vector<std::complex<float>>& out,
        const std::vector<float>& taps, int factor,
        std::vector<std::complex<float>>& history);
    void applyComplexFIR(
        const std::vector<std::complex<float>>& in,
        std::vector<std::complex<float>>& out,
        const std::vector<float>& taps,
        std::vector<std::complex<float>>& history);
    void decimateReal(
        const std::vector<float>& in,
        std::vector<float>& out,
        const std::vector<float>& taps, int factor,
        std::vector<float>& history);
    void applyFIR(
        const std::vector<float>& in,
        std::vector<float>& out,
        const std::vector<float>& taps,
        std::vector<float>& history);
    std::vector<float> amDemod(const std::vector<std::complex<float>>& signal);
    static std::vector<float> resample(const std::vector<float>& in, double inRate, double outRate);
};

#endif // AMDEMODULATOR_H
