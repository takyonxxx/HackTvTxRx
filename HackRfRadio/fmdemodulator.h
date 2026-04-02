#ifndef FMDEMODULATOR_H
#define FMDEMODULATOR_H

#include <QObject>
#include <complex>
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class FMDemodulator : public QObject
{
    Q_OBJECT

public:
    explicit FMDemodulator(double inputSampleRate, double bandwidth = 12500.0, QObject *parent = nullptr);

    std::vector<float> demodulate(const std::vector<std::complex<float>>& samples);

    void setSampleRate(double newRate);
    void setBandwidth(double bandwidthHz);
    double bandwidth() const { return m_bandwidth; }
    void setOutputGain(float gain) { m_outputGain = gain; }
    float outputGain() const { return m_outputGain; }
    void setDeemphTau(float tauUs) { m_deemphTau = tauUs * 1e-6f; } // input in microseconds
    float deemphTauUs() const { return m_deemphTau * 1e6f; }
    void setRxModIndex(float idx) { m_rxModIndex = idx; }
    float rxModIndex() const { return m_rxModIndex; }

private:
    struct DecimStage {
        std::vector<float> taps;
        int factor;
        double outputRate;
    };

    double m_inputRate;
    double m_bandwidth;
    float m_lastPhase;
    float m_outputGain;
    float m_rxModIndex = 1.0f;  // RX modulation index (FM sensitivity multiplier)
    float m_deemphTau;    // de-emphasis time constant in seconds (0 = off)
    float m_deemphPrev;   // de-emphasis filter state

    std::vector<DecimStage> m_iqStages;
    std::vector<DecimStage> m_realStages;
    std::vector<float> m_audioFilterTaps;
    std::vector<float> m_iqBandwidthTaps;

    float m_dcX1 = 0.0f;
    float m_dcY1 = 0.0f;

    void rebuildChain();

    static std::vector<float> designLPF(int numTaps, float cutoff, float sampleRate);
    static std::vector<std::complex<float>> decimateComplex(
        const std::vector<std::complex<float>>& in,
        const std::vector<float>& taps, int factor);
    static std::vector<std::complex<float>> applyComplexFIR(
        const std::vector<std::complex<float>>& in,
        const std::vector<float>& taps);
    static std::vector<float> decimateReal(
        const std::vector<float>& in,
        const std::vector<float>& taps, int factor);
    static std::vector<float> applyFIR(const std::vector<float>& in, const std::vector<float>& taps);
    std::vector<float> fmDemod(const std::vector<std::complex<float>>& signal, double rate);
    static std::vector<float> resample(const std::vector<float>& in, double inRate, double outRate);
    void removeDC(std::vector<float>& audio);
};

#endif // FMDEMODULATOR_H
