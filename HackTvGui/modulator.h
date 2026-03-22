#ifndef MODULATOR_H
#define MODULATOR_H

#include <QObject>
#include <QDebug>
#include <QMutex>
#include <complex>
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>
#include <array>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// WBFMDemodulator - Wideband FM demodulator
//
// Pipeline (same proven approach as PALBDecoder AudioDemodulator):
//   1. Multi-stage complex IQ decimation with anti-alias FIR filters
//      (e.g. 16 MHz -> 1.6 MHz -> 160 kHz)
//   2. FM demodulation at narrowband rate (~160-300 kHz)
//   3. Real-valued decimation stages to reach ~48 kHz
//   4. Final resample to exactly 48 kHz
//   5. Audio LPF at 15 kHz
//
// This produces clean FM audio because FM demod operates on a narrowband
// signal, not the wideband IQ data where video/noise cause huge phase jumps.
// ============================================================================

class WBFMDemodulator : public QObject
{
    Q_OBJECT

public:
    explicit WBFMDemodulator(double inputSampleRate, double bandwidth = 300000.0, QObject *parent = nullptr)
        : QObject(parent)
        , m_inputRate(inputSampleRate)
        , m_bandwidth(bandwidth)
        , m_lastPhase(0.0f)
    {
        rebuildChain();

        qDebug() << "WBFMDemodulator: input" << m_inputRate / 1e6 << "MHz"
                 << "bw" << m_bandwidth / 1e3 << "kHz"
                 << "stages:" << m_iqStages.size() << "IQ +"
                 << m_realStages.size() << "real";
    }

    // Main processing: IQ samples in, audio float samples out (48 kHz)
    std::vector<float> demodulate(const std::vector<std::complex<float>>& samples)
    {
        if (samples.empty()) return {};

        // 1. Multi-stage complex IQ decimation (fixed, for efficiency)
        auto iq = samples;
        for (const auto& stage : m_iqStages) {
            iq = decimateComplex(iq, stage.taps, stage.factor);
        }

        // 2. Adjustable IQ bandwidth filter (controls actual filter width)
        if (!m_iqBandwidthTaps.empty()) {
            iq = applyComplexFIR(iq, m_iqBandwidthTaps);
        }

        // 3. FM demodulate at narrowband rate
        double fmRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;
        auto audio = fmDemod(iq, fmRate);

        // 4. Real-valued decimation stages
        for (const auto& stage : m_realStages) {
            audio = decimateReal(audio, stage.taps, stage.factor);
        }

        // 5. Resample to 48 kHz
        double lastRate = m_realStages.empty()
                              ? (m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate)
                              : m_realStages.back().outputRate;
        if (std::abs(lastRate - 48000.0) > 1.0) {
            audio = resample(audio, lastRate, 48000.0);
        }

        // 6. Final audio LPF
        audio = applyFIR(audio, m_audioFilterTaps);

        // 7. DC removal
        removeDC(audio);

        return audio;
    }

    void setSampleRate(double newRate) {
        m_inputRate = newRate;
        m_lastPhase = 0.0f;
        rebuildChain();
        qDebug() << "WBFMDemodulator: rate changed to" << m_inputRate / 1e6 << "MHz";
    }

    // Set filter bandwidth (from plotter filter drag)
    // bandwidthHz = cutoff frequency in Hz (e.g. 120000 for +/-120 kHz WBFM)
    void setBandwidth(double bandwidthHz) {
        // Clamp to reasonable range
        double bw = std::clamp(bandwidthHz, 10000.0, 500000.0);

        // Design IQ bandwidth filter at post-decimation rate
        double postDecimRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;

        // Cutoff must be < Nyquist of post-decimation rate
        float cutoff = static_cast<float>(std::min(bw, postDecimRate * 0.45));
        m_iqBandwidthTaps = designLPF(31, cutoff, static_cast<float>(postDecimRate));

        qDebug() << "WBFMDemodulator: filter BW" << cutoff / 1e3 << "kHz @" << postDecimRate / 1e3 << "kHz";
    }

private:
    struct DecimStage {
        std::vector<float> taps;
        int factor;
        double outputRate;
    };

    double m_inputRate;
    double m_bandwidth;     // plotter filter width (Hz)
    float m_lastPhase;

    std::vector<DecimStage> m_iqStages;   // complex IQ decimation (before FM demod)
    std::vector<DecimStage> m_realStages;  // real decimation (after FM demod)
    std::vector<float> m_audioFilterTaps;  // final 15 kHz LPF at 48 kHz
    std::vector<float> m_iqBandwidthTaps;  // adjustable IQ bandwidth filter (post-decimation)

    // DC blocker state
    float m_dcX1 = 0.0f;
    float m_dcY1 = 0.0f;

    // ========== Chain Builder ==========
    void rebuildChain()
    {
        m_iqStages.clear();
        m_realStages.clear();

        double rate = m_inputRate;

        // Phase 1: IQ decimation - bring rate down to ~300 kHz (fixed)
        // The actual filter bandwidth is controlled by m_iqBandwidthTaps (adjustable)
        static constexpr double IQ_TARGET = 300000.0;
        const int candidates[] = {10, 8, 5, 4, 3, 2};

        while (rate > IQ_TARGET * 2.0) {
            int best = 0;
            for (int f : candidates) {
                if (rate / f >= IQ_TARGET) {
                    best = f;
                    break;
                }
            }
            if (best < 2) break;

            double newRate = rate / best;
            float cutoff = static_cast<float>(newRate * 0.4);
            int taps = (best >= 8) ? 33 : (best >= 5) ? 21 : 17;

            DecimStage s;
            s.taps = designLPF(taps, cutoff, static_cast<float>(rate));
            s.factor = best;
            s.outputRate = newRate;
            m_iqStages.push_back(std::move(s));
            rate = newRate;
        }

        // Phase 2: Real decimation - bring rate from IQ output toward ~50 kHz
        const double REAL_TARGET = 50000.0;

        while (rate > REAL_TARGET * 2.0) {
            int best = 0;
            for (int f : candidates) {
                if (rate / f >= 48000.0) {
                    best = f;
                    break;
                }
            }
            if (best < 2) break;

            double newRate = rate / best;
            float cutoff = static_cast<float>(newRate * 0.4);
            int taps = (best >= 8) ? 33 : (best >= 5) ? 21 : 17;

            DecimStage s;
            s.taps = designLPF(taps, cutoff, static_cast<float>(rate));
            s.factor = best;
            s.outputRate = newRate;
            m_realStages.push_back(std::move(s));
            rate = newRate;
        }

        // Final audio filter: 15 kHz at 48 kHz
        m_audioFilterTaps = designLPF(31, 15000.0f, 48000.0f);

        // Default IQ bandwidth filter at post-decimation rate
        double postDecimRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;
        float defaultBW = static_cast<float>(std::min(m_bandwidth, postDecimRate * 0.45));
        if (defaultBW > 0) {
            m_iqBandwidthTaps = designLPF(31, defaultBW, static_cast<float>(postDecimRate));
        } else {
            m_iqBandwidthTaps.clear();
        }

        // Log
        double r = m_inputRate;
        for (size_t i = 0; i < m_iqStages.size(); i++) {
            qDebug() << "  IQ stage" << i << ":"
                     << r / 1e3 << "kHz /" << m_iqStages[i].factor
                     << "->" << m_iqStages[i].outputRate / 1e3 << "kHz";
            r = m_iqStages[i].outputRate;
        }
        qDebug() << "  FM demod at" << r / 1e3 << "kHz";
        for (size_t i = 0; i < m_realStages.size(); i++) {
            qDebug() << "  Real stage" << i << ":"
                     << r / 1e3 << "kHz /" << m_realStages[i].factor
                     << "->" << m_realStages[i].outputRate / 1e3 << "kHz";
            r = m_realStages[i].outputRate;
        }
        qDebug() << "  Resample" << r / 1e3 << "kHz -> 48 kHz";
    }

    // ========== FIR Filter Design (Hamming window) ==========
    static std::vector<float> designLPF(int numTaps, float cutoff, float sampleRate)
    {
        std::vector<float> h(numTaps);
        float fc = cutoff / sampleRate;
        int M = numTaps / 2;
        for (int n = 0; n < numTaps; n++) {
            float m = static_cast<float>(n - M);
            h[n] = (m == 0.0f) ? 2.0f * fc
                               : std::sin(2.0f * M_PI * fc * m) / (M_PI * m);
            h[n] *= 0.54f - 0.46f * std::cos(2.0f * M_PI * n / (numTaps - 1));
        }
        float sum = std::accumulate(h.begin(), h.end(), 0.0f);
        if (sum != 0.0f) for (auto& t : h) t /= sum;
        return h;
    }

    // ========== Complex IQ decimation ==========
    static std::vector<std::complex<float>> decimateComplex(
        const std::vector<std::complex<float>>& in,
        const std::vector<float>& taps, int factor)
    {
        const size_t N = in.size();
        const size_t T = taps.size();
        const int half = T / 2;

        std::vector<std::complex<float>> out;
        out.reserve(N / factor + 1);

        for (size_t i = 0; i < N; i += factor) {
            float sumR = 0.0f, sumI = 0.0f;
            for (size_t j = 0; j < T; j++) {
                int idx = static_cast<int>(i) - half + static_cast<int>(j);
                if (idx >= 0 && idx < static_cast<int>(N)) {
                    sumR += in[idx].real() * taps[j];
                    sumI += in[idx].imag() * taps[j];
                }
            }
            out.emplace_back(sumR, sumI);
        }
        return out;
    }

    // ========== Complex FIR filter (no decimation) ==========
    static std::vector<std::complex<float>> applyComplexFIR(
        const std::vector<std::complex<float>>& in,
        const std::vector<float>& taps)
    {
        if (in.empty() || taps.empty()) return in;
        const size_t N = in.size();
        const size_t T = taps.size();
        const int half = T / 2;

        std::vector<std::complex<float>> out(N);
        for (size_t i = 0; i < N; i++) {
            float sumR = 0.0f, sumI = 0.0f;
            for (size_t j = 0; j < T; j++) {
                int idx = static_cast<int>(i) - half + static_cast<int>(j);
                if (idx >= 0 && idx < static_cast<int>(N)) {
                    sumR += in[idx].real() * taps[j];
                    sumI += in[idx].imag() * taps[j];
                }
            }
            out[i] = {sumR, sumI};
        }
        return out;
    }

    // ========== Real decimation ==========
    static std::vector<float> decimateReal(
        const std::vector<float>& in,
        const std::vector<float>& taps, int factor)
    {
        const size_t N = in.size();
        const size_t T = taps.size();
        const int half = T / 2;

        std::vector<float> out;
        out.reserve(N / factor + 1);

        for (size_t i = 0; i < N; i += factor) {
            float sum = 0.0f;
            for (size_t j = 0; j < T; j++) {
                int idx = static_cast<int>(i) - half + static_cast<int>(j);
                if (idx >= 0 && idx < static_cast<int>(N)) {
                    sum += in[idx] * taps[j];
                }
            }
            out.push_back(sum);
        }
        return out;
    }

    // ========== Apply FIR (no decimation) ==========
    static std::vector<float> applyFIR(const std::vector<float>& in, const std::vector<float>& taps)
    {
        if (in.empty() || taps.empty()) return in;
        const size_t N = in.size();
        const size_t T = taps.size();
        const int half = T / 2;

        std::vector<float> out(N);
        for (size_t i = 0; i < N; i++) {
            float sum = 0.0f;
            for (size_t j = 0; j < T; j++) {
                int idx = static_cast<int>(i) - half + static_cast<int>(j);
                if (idx >= 0 && idx < static_cast<int>(N)) {
                    sum += in[idx] * taps[j];
                }
            }
            out[i] = sum;
        }
        return out;
    }

    // ========== FM Demodulation (narrowband, atan2) ==========
    std::vector<float> fmDemod(const std::vector<std::complex<float>>& signal, double rate)
    {
        if (signal.empty()) return {};

        std::vector<float> out(signal.size());

        static constexpr float FM_OUTPUT_SCALE = 0.3f;

        float prev = m_lastPhase;
        for (size_t i = 0; i < signal.size(); i++) {
            float phase = std::atan2(signal[i].imag(), signal[i].real());
            float delta = phase - prev;
            // Unwrap
            if (delta > M_PI) delta -= 2.0f * M_PI;
            if (delta < -M_PI) delta += 2.0f * M_PI;
            out[i] = delta * FM_OUTPUT_SCALE;
            prev = phase;
        }
        m_lastPhase = prev;

        return out;
    }

    // ========== Linear interpolation resample ==========
    static std::vector<float> resample(const std::vector<float>& in, double inRate, double outRate)
    {
        if (in.empty()) return in;
        double ratio = inRate / outRate;
        size_t outN = static_cast<size_t>(in.size() / ratio);
        std::vector<float> out;
        out.reserve(outN);
        for (size_t i = 0; i < outN; i++) {
            double pos = i * ratio;
            size_t idx = static_cast<size_t>(pos);
            double frac = pos - idx;
            if (idx + 1 < in.size())
                out.push_back(in[idx] * (1.0 - frac) + in[idx + 1] * frac);
            else if (idx < in.size())
                out.push_back(in[idx]);
        }
        return out;
    }

    // ========== DC removal ==========
    void removeDC(std::vector<float>& audio)
    {
        constexpr float alpha = 0.995f;
        for (auto& s : audio) {
            float y = s - m_dcX1 + alpha * m_dcY1;
            m_dcX1 = s;
            m_dcY1 = y;
            s = y;
        }
    }
};


// ============================================================================
// Legacy compatibility wrappers
// ============================================================================

class FMDemodulator : public QObject
{
    Q_OBJECT
public:
    explicit FMDemodulator(double quadratureRate, int audioDecimation, QObject *parent = nullptr)
        : QObject(parent) { (void)quadratureRate; (void)audioDecimation; }

    std::vector<float> demodulate(const std::vector<std::complex<float>>& samples)
    {
        (void)samples;
        return {};
    }
};

class RationalResampler {
public:
    RationalResampler(int interpolation, int decimation) {
        (void)interpolation; (void)decimation;
    }
    std::vector<std::complex<float>> resample(const std::vector<std::complex<float>>& input) {
        return input;
    }
};

class LowPassFilter {
public:
    LowPassFilter(double sampleRate, double cutoffFreq, double transitionWidth) {
        (void)sampleRate; (void)cutoffFreq; (void)transitionWidth;
    }
    std::vector<std::complex<float>> apply(const std::vector<std::complex<float>>& input) {
        return input;
    }
    void designFilter(double sampleRate, double cutoffFreq, double transitionWidth) {
        (void)sampleRate; (void)cutoffFreq; (void)transitionWidth;
    }
};

#endif // MODULATOR_H
