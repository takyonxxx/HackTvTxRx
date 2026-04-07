#ifndef FMDEMODULATOR_H
#define FMDEMODULATOR_H

#include <QObject>
#include <complex>
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <atomic>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class FMDemodulator : public QObject
{
    Q_OBJECT

public:
    explicit FMDemodulator(double inputSampleRate, double bandwidth = 12500.0, QObject *parent = nullptr);

    // Returns interleaved stereo samples [L,R,L,R,...] for WBFM when stereo detected
    // Returns interleaved mono-duplicated [M,M,M,M,...] for NBFM/mono
    std::vector<float> demodulate(const std::vector<std::complex<float>>& samples);

    void setSampleRate(double newRate);
    void setBandwidth(double bandwidthHz);
    double bandwidth() const { return m_bandwidth; }
    void setOutputGain(float gain) { m_outputGain = gain; }
    float outputGain() const { return m_outputGain; }
    void setDeemphTau(float tauUs) { m_deemphTau = tauUs * 1e-6f; }
    float deemphTauUs() const { return m_deemphTau * 1e6f; }
    void setRxModIndex(float idx) { m_rxModIndex = idx; }
    float rxModIndex() const { return m_rxModIndex; }
    void setAudioLPF(float cutoffHz);
    float audioLPF() const { return m_audioLpfCutoff; }

    void setFMNR(bool enabled) { m_fmnrEnabled = enabled; }
    bool fmnrEnabled() const { return m_fmnrEnabled; }

    bool isStereo() const { return m_stereoDetected.load(); }
    void setForceMono(bool mono) { m_forceMono = mono; }

signals:
    void stereoStatusChanged(bool stereo);

private:
    struct DecimStage {
        std::vector<float> taps;
        int factor;
        double outputRate;
        // Persistent delay line for block-continuous filtering
        std::vector<std::complex<float>> iqHistory;   // for complex decimation
        std::vector<float> realHistory;                // for real decimation
    };

    double m_inputRate;
    double m_bandwidth;
    float m_lastPhase;
    float m_outputGain;
    float m_rxModIndex = 1.0f;
    float m_prevClipSample = 0.0f;
    float m_audioLpfCutoff = 5000.0f;
    float m_deemphTau;
    float m_deemphPrevL;  // de-emphasis L channel
    float m_deemphPrevR;  // de-emphasis R channel
    float m_hpfPrevL = 0.0f, m_hpfPrevInL = 0.0f;  // HPF L
    float m_hpfPrevR = 0.0f, m_hpfPrevInR = 0.0f;  // HPF R

    std::vector<DecimStage> m_iqStages;
    std::vector<DecimStage> m_realStages;  // only for NBFM
    std::vector<float> m_audioFilterTaps;
    std::vector<float> m_iqBandwidthTaps;

    // Persistent delay lines for non-decimating FIR filters
    std::vector<std::complex<float>> m_iqBwHistory;   // IQ bandwidth filter state
    std::vector<float> m_audioFilterHistory;            // audio LPF state
    std::vector<float> m_monoFilterHistory;             // WBFM mono LPF state
    std::vector<float> m_diffFilterHistory;             // WBFM diff LPF state

    // Stereo decode state
    double m_pilotPhase = 0.0;       // PLL phase accumulator for 19 kHz pilot
    double m_pilotFreq = 19000.0;    // PLL frequency estimate
    float m_pilotLevel = 0.0f;       // pilot energy for detection
    std::atomic<bool> m_stereoDetected{false};
    bool m_forceMono = false;
    std::vector<float> m_monoFilterTaps;   // LPF for L+R (15 kHz)
    std::vector<float> m_diffFilterTaps;   // LPF for L-R (15 kHz)

    float m_dcX1L = 0.0f, m_dcY1L = 0.0f;
    float m_dcX1R = 0.0f, m_dcY1R = 0.0f;

    double m_mpxRate = 0.0;  // rate after IQ decimation (before stereo decode)

    // FM IF Noise Reduction (amplitude limiter + smoothing)
    bool m_fmnrEnabled = false;
    std::vector<std::complex<float>> m_fmnrBuffer;  // history for moving average

    void rebuildChain();
    void applyFMNR(std::vector<std::complex<float>>& iq);

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
    std::vector<float> fmDemod(const std::vector<std::complex<float>>& signal, double rate);
    std::vector<float> decodeStereo(const std::vector<float>& mpx, double mpxRate);
    static std::vector<float> resample(const std::vector<float>& in, double inRate, double outRate);
    void removeDC(float& dcX1, float& dcY1, std::vector<float>& audio);
};

#endif // FMDEMODULATOR_H
