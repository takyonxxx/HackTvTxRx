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
// WBFMDemodulator - Wideband/Narrowband FM demodulator
//
// All HackRfRadio improvements integrated:
//   - Conjugate multiply FM demod (cleaner than raw atan2)
//   - Rate-normalized output (consistent level regardless of decimation)
//   - Adjustable outputGain, rxModIndex, deemphTau
//   - 250 Hz high-pass filter for voice clarity
//   - Soft limiter to prevent clipping
//   - 51-tap filters for NFM, sharper cutoff
//   - NFM/WFM auto-detect from bandwidth
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
        , m_outputGain(0.0f)
        , m_rxModIndex(1.0f)
        , m_deemphTau(0.0f)
        , m_deemphPrevL(0.0f)
        , m_deemphPrevR(0.0f)
    {
        rebuildChain();

        qDebug() << "WBFMDemodulator: input" << m_inputRate / 1e6 << "MHz"
                 << "bw" << m_bandwidth / 1e3 << "kHz"
                 << "stages:" << m_iqStages.size() << "IQ +"
                 << m_realStages.size() << "real";
    }

    // Returns interleaved stereo [L,R,L,R,...] — stereo when pilot detected, mono-dup otherwise
    std::vector<float> demodulate(const std::vector<std::complex<float>>& samples)
    {
        if (samples.empty()) return {};

        bool isWBFM = (m_bandwidth > 25000.0);

        // 1. Multi-stage complex IQ decimation
        auto iq = samples;
        for (const auto& stage : m_iqStages) {
            iq = decimateComplex(iq, stage.taps, stage.factor);
        }

        // 2. Adjustable IQ bandwidth filter
        if (!m_iqBandwidthTaps.empty()) {
            iq = applyComplexFIR(iq, m_iqBandwidthTaps);
        }

        // 3. FM demodulate — produces MPX baseband
        double fmRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;
        auto mpx = fmDemod(iq, fmRate);

        // WBFM stereo decode at MPX rate
        if (isWBFM && fmRate >= 76000.0) {
            return decodeStereo(mpx, fmRate);
        }

        // NBFM / mono fallback
        for (const auto& stage : m_realStages) {
            mpx = decimateReal(mpx, stage.taps, stage.factor);
        }

        double lastRate = m_realStages.empty() ? fmRate : m_realStages.back().outputRate;
        if (std::abs(lastRate - 48000.0) > 1.0) {
            mpx = resample(mpx, lastRate, 48000.0);
        }

        mpx = applyFIR(mpx, m_audioFilterTaps);

        // HPF
        {
            float cutoff = 250.0f;
            float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * cutoff);
            float dt = 1.0f / 48000.0f;
            float alpha = rc / (rc + dt);
            for (auto& s : mpx) {
                float f = alpha * (m_hpfPrevL + s - m_hpfPrevInL);
                m_hpfPrevInL = s; m_hpfPrevL = f; s = f;
            }
        }

        // De-emphasis
        if (m_deemphTau > 0.0f) {
            float dt = 1.0f / 48000.0f;
            float alphaD = m_deemphTau / (m_deemphTau + dt);
            for (auto& s : mpx) {
                m_deemphPrevL = alphaD * m_deemphPrevL + (1.0f - alphaD) * s;
                s = m_deemphPrevL;
            }
        }

        removeDC(m_dcX1L, m_dcY1L, mpx);

        // Mono → interleaved stereo
        std::vector<float> out(mpx.size() * 2);
        for (size_t i = 0; i < mpx.size(); i++) {
            float s = mpx[i];
            if (s > 0.9f) s = 0.9f + 0.1f * std::tanh((s - 0.9f) * 8.0f);
            else if (s < -0.9f) s = -0.9f + 0.1f * std::tanh((s + 0.9f) * 8.0f);
            out[i * 2] = s;
            out[i * 2 + 1] = s;
        }

        if (m_stereoDetected) {
            m_stereoDetected = false;
            emit stereoStatusChanged(false);
        }
        return out;
    }

    void setSampleRate(double newRate) {
        m_inputRate = newRate;
        m_lastPhase = 0.0f;
        m_pilotPhase = 0.0;
        m_pilotFreq = 19000.0;
        m_pilotLevel = 0.0f;
        rebuildChain();
        qDebug() << "WBFMDemodulator: rate changed to" << m_inputRate / 1e6 << "MHz";
    }

    void setBandwidth(double bandwidthHz) {
        m_bandwidth = std::clamp(bandwidthHz, 5000.0, 500000.0);
        rebuildChain();
        qDebug() << "WBFMDemodulator: BW" << m_bandwidth / 1e3 << "kHz";
    }

    double bandwidth() const { return m_bandwidth; }
    void setOutputGain(float gain) { m_outputGain = gain; }
    float outputGain() const { return m_outputGain; }
    void setRxModIndex(float idx) { m_rxModIndex = idx; }
    float rxModIndex() const { return m_rxModIndex; }
    void setDeemphTau(float tauUs) { m_deemphTau = tauUs * 1e-6f; }
    float deemphTauUs() const { return m_deemphTau * 1e6f; }
    bool isStereo() const { return m_stereoDetected; }
    void setForceMono(bool mono) { m_forceMono = mono; }

signals:
    void stereoStatusChanged(bool stereo);

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
    float m_rxModIndex;
    float m_deemphTau;
    float m_deemphPrevL, m_deemphPrevR;
    float m_hpfPrevL = 0.0f, m_hpfPrevInL = 0.0f;
    float m_hpfPrevR = 0.0f, m_hpfPrevInR = 0.0f;

    std::vector<DecimStage> m_iqStages;
    std::vector<DecimStage> m_realStages;
    std::vector<float> m_audioFilterTaps;
    std::vector<float> m_iqBandwidthTaps;
    std::vector<float> m_monoFilterTaps;
    std::vector<float> m_diffFilterTaps;

    float m_dcX1L = 0.0f, m_dcY1L = 0.0f;
    float m_dcX1R = 0.0f, m_dcY1R = 0.0f;

    // Stereo PLL state
    double m_pilotPhase = 0.0;
    double m_pilotFreq = 19000.0;
    float m_pilotLevel = 0.0f;
    bool m_stereoDetected = false;
    bool m_forceMono = false;

    // ========== Stereo Decode ==========
    std::vector<float> decodeStereo(const std::vector<float>& mpx, double mpxRate)
    {
        const size_t N = mpx.size();

        // Pilot detection via Goertzel at 19 kHz
        float pilotEnergy = 0.0f;
        {
            float k = 19000.0f / static_cast<float>(mpxRate) * N;
            float w = 2.0f * static_cast<float>(M_PI) * k / N;
            float coeff = 2.0f * std::cos(w);
            float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;
            for (size_t i = 0; i < N; i++) {
                s0 = mpx[i] + coeff * s1 - s2;
                s2 = s1; s1 = s0;
            }
            pilotEnergy = (s1 * s1 + s2 * s2 - coeff * s1 * s2) / (N * N);
        }

        m_pilotLevel = 0.8f * m_pilotLevel + 0.2f * pilotEnergy;
        bool stereoNow = (m_pilotLevel > 1e-8f);

        if (stereoNow != m_stereoDetected) {
            m_stereoDetected = stereoNow;
            emit stereoStatusChanged(stereoNow);
        }

        bool doStereo = stereoNow && !m_forceMono;

        // L+R extraction
        auto monoSignal = applyFIR(mpx, m_monoFilterTaps);

        std::vector<float> leftAudio, rightAudio;

        if (doStereo) {
            // PLL + 38 kHz demod for L-R
            std::vector<float> diffRaw(N);
            for (size_t i = 0; i < N; i++) {
                float ref = static_cast<float>(std::cos(m_pilotPhase * 2.0));
                diffRaw[i] = mpx[i] * ref * 2.0f;

                float pilotRef = static_cast<float>(std::sin(m_pilotPhase));
                float pilotError = mpx[i] * pilotRef;
                double alpha = 50.0 / mpxRate;
                m_pilotFreq += alpha * pilotError * 0.1;
                m_pilotFreq = std::clamp(m_pilotFreq, 18900.0, 19100.0);
                m_pilotPhase += 2.0 * M_PI * m_pilotFreq / mpxRate;
                if (m_pilotPhase > 2.0 * M_PI) m_pilotPhase -= 2.0 * M_PI;
            }

            auto diffSignal = applyFIR(diffRaw, m_diffFilterTaps);
            size_t len = std::min(monoSignal.size(), diffSignal.size());
            leftAudio.resize(len);
            rightAudio.resize(len);
            for (size_t i = 0; i < len; i++) {
                leftAudio[i]  = monoSignal[i] + diffSignal[i];
                rightAudio[i] = monoSignal[i] - diffSignal[i];
            }
        } else {
            leftAudio = monoSignal;
            rightAudio = monoSignal;
        }

        // Resample to 48 kHz
        if (std::abs(mpxRate - 48000.0) > 1.0) {
            leftAudio = resample(leftAudio, mpxRate, 48000.0);
            rightAudio = resample(rightAudio, mpxRate, 48000.0);
        }

        // Per-channel de-emphasis
        if (m_deemphTau > 0.0f) {
            float dt = 1.0f / 48000.0f;
            float alphaD = m_deemphTau / (m_deemphTau + dt);
            for (auto& s : leftAudio) { m_deemphPrevL = alphaD * m_deemphPrevL + (1.0f - alphaD) * s; s = m_deemphPrevL; }
            for (auto& s : rightAudio) { m_deemphPrevR = alphaD * m_deemphPrevR + (1.0f - alphaD) * s; s = m_deemphPrevR; }
        }

        // Per-channel HPF
        {
            float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * 250.0f);
            float dt = 1.0f / 48000.0f;
            float alpha = rc / (rc + dt);
            for (auto& s : leftAudio) { float f = alpha * (m_hpfPrevL + s - m_hpfPrevInL); m_hpfPrevInL = s; m_hpfPrevL = f; s = f; }
            for (auto& s : rightAudio) { float f = alpha * (m_hpfPrevR + s - m_hpfPrevInR); m_hpfPrevInR = s; m_hpfPrevR = f; s = f; }
        }

        removeDC(m_dcX1L, m_dcY1L, leftAudio);
        removeDC(m_dcX1R, m_dcY1R, rightAudio);

        // Interleave + soft limiter
        size_t outLen = std::min(leftAudio.size(), rightAudio.size());
        std::vector<float> out(outLen * 2);
        for (size_t i = 0; i < outLen; i++) {
            float l = leftAudio[i], r = rightAudio[i];
            if (l > 0.9f) l = 0.9f + 0.1f * std::tanh((l - 0.9f) * 8.0f);
            else if (l < -0.9f) l = -0.9f + 0.1f * std::tanh((l + 0.9f) * 8.0f);
            if (r > 0.9f) r = 0.9f + 0.1f * std::tanh((r - 0.9f) * 8.0f);
            else if (r < -0.9f) r = -0.9f + 0.1f * std::tanh((r + 0.9f) * 8.0f);
            out[i * 2] = l;
            out[i * 2 + 1] = r;
        }
        return out;
    }

    // ========== Chain Builder ==========
    void rebuildChain()
    {
        m_iqStages.clear();
        m_realStages.clear();

        bool isNBFM = m_bandwidth <= 25000.0;
        bool isWBFM = !isNBFM;

        double rate = m_inputRate;

        double iqTarget = isNBFM ? 200000.0 : 300000.0;
        iqTarget = std::min(iqTarget, 400000.0);
        const int candidates[] = {10, 8, 5, 4, 3, 2};

        while (rate > iqTarget * 2.0) {
            int best = 0;
            for (int f : candidates) {
                if (rate / f >= iqTarget) { best = f; break; }
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

        if (isWBFM) {
            // No real decimation — stereo decode needs high rate
            m_monoFilterTaps = designLPF(63, 15000.0f, static_cast<float>(rate));
            m_diffFilterTaps = designLPF(63, 15000.0f, static_cast<float>(rate));
            m_audioFilterTaps = designLPF(31, 15000.0f, 48000.0f);
            if (m_outputGain <= 0.0f) m_outputGain = 0.5f;
        } else {
            // NBFM: real decimation to ~48 kHz
            while (rate > 100000.0) {
                int best = 0;
                for (int f : candidates) {
                    if (rate / f >= 48000.0) { best = f; break; }
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

            m_audioFilterTaps = designLPF(51, 3500.0f, 48000.0f);
            if (m_outputGain <= 0.0f) m_outputGain = 2.0f;
            m_monoFilterTaps.clear();
            m_diffFilterTaps.clear();
        }

        // IQ bandwidth filter
        double postDecimRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;
        float filterBW = static_cast<float>(std::min(m_bandwidth, postDecimRate * 0.45));
        int iqFilterTaps = isNBFM ? 51 : 31;
        if (filterBW > 0) {
            m_iqBandwidthTaps = designLPF(iqFilterTaps, filterBW, static_cast<float>(postDecimRate));
        } else {
            m_iqBandwidthTaps.clear();
        }

        qDebug() << "WBFMDemodulator: rebuilt -" << (isNBFM ? "NFM" : "WFM")
                 << "gain=" << m_outputGain
                 << "postDecim=" << postDecimRate / 1e3 << "kHz";
    }

    // ========== Utilities ==========
    static std::vector<float> designLPF(int numTaps, float cutoff, float sampleRate)
    {
        std::vector<float> h(numTaps);
        float fc = cutoff / sampleRate;
        int M = numTaps / 2;
        for (int n = 0; n < numTaps; n++) {
            float m = static_cast<float>(n - M);
            h[n] = (m == 0.0f) ? 2.0f * fc
                               : std::sin(2.0f * static_cast<float>(M_PI) * fc * m) / (static_cast<float>(M_PI) * m);
            h[n] *= 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * n / (numTaps - 1));
        }
        float sum = std::accumulate(h.begin(), h.end(), 0.0f);
        if (sum != 0.0f) for (auto& t : h) t /= sum;
        return h;
    }

    static std::vector<std::complex<float>> decimateComplex(
        const std::vector<std::complex<float>>& in, const std::vector<float>& taps, int factor)
    {
        const size_t N = in.size(), T = taps.size();
        const int half = T / 2;
        std::vector<std::complex<float>> out;
        out.reserve(N / factor + 1);
        for (size_t i = 0; i < N; i += factor) {
            float sR = 0.0f, sI = 0.0f;
            for (size_t j = 0; j < T; j++) {
                int idx = static_cast<int>(i) - half + static_cast<int>(j);
                if (idx >= 0 && idx < static_cast<int>(N)) { sR += in[idx].real() * taps[j]; sI += in[idx].imag() * taps[j]; }
            }
            out.emplace_back(sR, sI);
        }
        return out;
    }

    static std::vector<std::complex<float>> applyComplexFIR(
        const std::vector<std::complex<float>>& in, const std::vector<float>& taps)
    {
        if (in.empty() || taps.empty()) return in;
        const size_t N = in.size(), T = taps.size();
        const int half = T / 2;
        std::vector<std::complex<float>> out(N);
        for (size_t i = 0; i < N; i++) {
            float sR = 0.0f, sI = 0.0f;
            for (size_t j = 0; j < T; j++) {
                int idx = static_cast<int>(i) - half + static_cast<int>(j);
                if (idx >= 0 && idx < static_cast<int>(N)) { sR += in[idx].real() * taps[j]; sI += in[idx].imag() * taps[j]; }
            }
            out[i] = {sR, sI};
        }
        return out;
    }

    static std::vector<float> decimateReal(const std::vector<float>& in, const std::vector<float>& taps, int factor)
    {
        const size_t N = in.size(), T = taps.size();
        const int half = T / 2;
        std::vector<float> out;
        out.reserve(N / factor + 1);
        for (size_t i = 0; i < N; i += factor) {
            float sum = 0.0f;
            for (size_t j = 0; j < T; j++) { int idx = static_cast<int>(i) - half + static_cast<int>(j); if (idx >= 0 && idx < static_cast<int>(N)) sum += in[idx] * taps[j]; }
            out.push_back(sum);
        }
        return out;
    }

    static std::vector<float> applyFIR(const std::vector<float>& in, const std::vector<float>& taps)
    {
        if (in.empty() || taps.empty()) return in;
        const size_t N = in.size(), T = taps.size();
        const int half = T / 2;
        std::vector<float> out(N);
        for (size_t i = 0; i < N; i++) {
            float sum = 0.0f;
            for (size_t j = 0; j < T; j++) { int idx = static_cast<int>(i) - half + static_cast<int>(j); if (idx >= 0 && idx < static_cast<int>(N)) sum += in[idx] * taps[j]; }
            out[i] = sum;
        }
        return out;
    }

    std::vector<float> fmDemod(const std::vector<std::complex<float>>& signal, double rate)
    {
        if (signal.empty()) return {};
        std::vector<float> out(signal.size());
        float rateNorm = static_cast<float>(rate) / (2.0f * static_cast<float>(M_PI));
        float refDeviation = (m_bandwidth <= 25000.0) ? 5000.0f : 75000.0f;
        float scale = (rateNorm / refDeviation) * m_outputGain * m_rxModIndex;
        std::complex<float> prevSample(std::cos(m_lastPhase), std::sin(m_lastPhase));
        for (size_t i = 0; i < signal.size(); i++) {
            std::complex<float> product = signal[i] * std::conj(prevSample);
            out[i] = std::atan2(product.imag(), product.real()) * scale;
            prevSample = signal[i];
        }
        m_lastPhase = std::atan2(prevSample.imag(), prevSample.real());
        return out;
    }

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
            if (idx + 1 < in.size()) out.push_back(static_cast<float>(in[idx] * (1.0 - frac) + in[idx + 1] * frac));
            else if (idx < in.size()) out.push_back(in[idx]);
        }
        return out;
    }

    void removeDC(float& dcX1, float& dcY1, std::vector<float>& audio)
    {
        constexpr float alpha = 0.995f;
        for (auto& s : audio) { float y = s - dcX1 + alpha * dcY1; dcX1 = s; dcY1 = y; s = y; }
    }
};


// ============================================================================
// AMDemodulator - AM envelope demodulator for air band (108-136 MHz)
//
//   - Multi-stage IQ decimation (reuses WBFMDemodulator utilities)
//   - Envelope detection via magnitude: |I + jQ|
//   - DC removal for carrier stripping
//   - AGC (Automatic Gain Control) for consistent output level
//   - Audio LPF + optional HPF for voice clarity
//   - Mono output as interleaved stereo [L,R,L,R,...]
// ============================================================================

class AMDemodulator : public QObject
{
    Q_OBJECT

public:
    explicit AMDemodulator(double inputSampleRate, double bandwidth = 10000.0, QObject *parent = nullptr)
        : QObject(parent)
        , m_inputRate(inputSampleRate)
        , m_bandwidth(bandwidth)
        , m_outputGain(1.0f)
        , m_agcGain(1.0f)
        , m_agcTarget(0.3f)
        , m_agcAttack(0.01f)
        , m_agcDecay(0.0001f)
        , m_dcAlpha(0.995f)
        , m_dcPrev(0.0f)
        , m_dcOut(0.0f)
    {
        rebuildChain();

        qDebug() << "AMDemodulator: input" << m_inputRate / 1e6 << "MHz"
                 << "bw" << m_bandwidth / 1e3 << "kHz"
                 << "iqStages:" << m_iqStages.size()
                 << "realStages:" << m_realStages.size();
    }

    // Returns interleaved stereo [L,R,L,R,...] (mono duplicated)
    std::vector<float> demodulate(const std::vector<std::complex<float>>& samples)
    {
        if (samples.empty()) return {};

        // 1. Multi-stage complex IQ decimation
        auto iq = samples;
        for (const auto& stage : m_iqStages) {
            iq = decimateComplex(iq, stage.taps, stage.factor);
        }

        // 2. IQ bandwidth filter
        if (!m_iqBandwidthTaps.empty()) {
            iq = applyComplexFIR(iq, m_iqBandwidthTaps);
        }

        // 3. AM envelope detection: magnitude of complex signal
        std::vector<float> envelope(iq.size());
        for (size_t i = 0; i < iq.size(); i++) {
            envelope[i] = std::sqrt(iq[i].real() * iq[i].real() + iq[i].imag() * iq[i].imag());
        }

        // 4. DC removal (strip carrier)
        for (auto& s : envelope) {
            float y = s - m_dcPrev + m_dcAlpha * m_dcOut;
            m_dcPrev = s;
            m_dcOut = y;
            s = y;
        }

        // 5. AGC - automatic gain control
        for (auto& s : envelope) {
            float absVal = std::abs(s);
            if (absVal > m_agcTarget) {
                m_agcGain -= m_agcAttack * (absVal - m_agcTarget);
            } else {
                m_agcGain += m_agcDecay * (m_agcTarget - absVal);
            }
            m_agcGain = std::clamp(m_agcGain, 0.01f, 50.0f);
            s *= m_agcGain;
        }

        // 6. Real-domain decimation to ~48 kHz
        for (const auto& stage : m_realStages) {
            envelope = decimateReal(envelope, stage.taps, stage.factor);
        }

        double lastRate = m_realStages.empty()
            ? (m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate)
            : m_realStages.back().outputRate;

        if (std::abs(lastRate - 48000.0) > 1.0) {
            envelope = resample(envelope, lastRate, 48000.0);
        }

        // 7. Audio LPF (voice band)
        envelope = applyFIR(envelope, m_audioFilterTaps);

        // 8. Apply output gain + soft limiter, duplicate to stereo
        std::vector<float> out(envelope.size() * 2);
        for (size_t i = 0; i < envelope.size(); i++) {
            float s = envelope[i] * m_outputGain;
            if (s > 0.9f) s = 0.9f + 0.1f * std::tanh((s - 0.9f) * 8.0f);
            else if (s < -0.9f) s = -0.9f + 0.1f * std::tanh((s + 0.9f) * 8.0f);
            out[i * 2] = s;
            out[i * 2 + 1] = s;
        }

        return out;
    }

    void setSampleRate(double newRate) {
        m_inputRate = newRate;
        m_agcGain = 1.0f;
        m_dcPrev = 0.0f;
        m_dcOut = 0.0f;
        rebuildChain();
        qDebug() << "AMDemodulator: rate changed to" << m_inputRate / 1e6 << "MHz";
    }

    void setBandwidth(double bandwidthHz) {
        m_bandwidth = std::clamp(bandwidthHz, 2000.0, 50000.0);
        rebuildChain();
        qDebug() << "AMDemodulator: BW" << m_bandwidth / 1e3 << "kHz";
    }

    double bandwidth() const { return m_bandwidth; }
    void setOutputGain(float gain) { m_outputGain = gain; }
    float outputGain() const { return m_outputGain; }

private:
    struct DecimStage {
        std::vector<float> taps;
        int factor;
        double outputRate;
    };

    double m_inputRate;
    double m_bandwidth;
    float m_outputGain;

    // AGC state
    float m_agcGain;
    float m_agcTarget;
    float m_agcAttack;
    float m_agcDecay;

    // DC removal state
    float m_dcAlpha;
    float m_dcPrev;
    float m_dcOut;

    std::vector<DecimStage> m_iqStages;
    std::vector<DecimStage> m_realStages;
    std::vector<float> m_audioFilterTaps;
    std::vector<float> m_iqBandwidthTaps;

    void rebuildChain()
    {
        m_iqStages.clear();
        m_realStages.clear();

        double rate = m_inputRate;

        // AM air band: narrower IQ target than FM
        double iqTarget = 100000.0;
        const int candidates[] = {10, 8, 5, 4, 3, 2};

        while (rate > iqTarget * 2.0) {
            int best = 0;
            for (int f : candidates) {
                if (rate / f >= iqTarget) { best = f; break; }
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

        // IQ bandwidth filter - narrow for AM voice
        double postDecimRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;
        float filterBW = static_cast<float>(std::min(m_bandwidth, postDecimRate * 0.45));
        if (filterBW > 0) {
            m_iqBandwidthTaps = designLPF(51, filterBW, static_cast<float>(postDecimRate));
        } else {
            m_iqBandwidthTaps.clear();
        }

        // Real decimation to ~48 kHz
        while (rate > 100000.0) {
            int best = 0;
            for (int f : candidates) {
                if (rate / f >= 48000.0) { best = f; break; }
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

        // Voice band audio filter: 300-5000 Hz typical for AM air band
        m_audioFilterTaps = designLPF(51, 5000.0f, 48000.0f);

        if (m_outputGain <= 0.0f) m_outputGain = 3.0f;

        qDebug() << "AMDemodulator: rebuilt - gain=" << m_outputGain
                 << "postDecim=" << postDecimRate / 1e3 << "kHz";
    }

    // ========== Utilities (same as WBFMDemodulator) ==========
    static std::vector<float> designLPF(int numTaps, float cutoff, float sampleRate)
    {
        std::vector<float> h(numTaps);
        float fc = cutoff / sampleRate;
        int M = numTaps / 2;
        for (int n = 0; n < numTaps; n++) {
            float m = static_cast<float>(n - M);
            h[n] = (m == 0.0f) ? 2.0f * fc
                               : std::sin(2.0f * static_cast<float>(M_PI) * fc * m) / (static_cast<float>(M_PI) * m);
            h[n] *= 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * n / (numTaps - 1));
        }
        float sum = std::accumulate(h.begin(), h.end(), 0.0f);
        if (sum != 0.0f) for (auto& t : h) t /= sum;
        return h;
    }

    static std::vector<std::complex<float>> decimateComplex(
        const std::vector<std::complex<float>>& in, const std::vector<float>& taps, int factor)
    {
        const size_t N = in.size(), T = taps.size();
        const int half = T / 2;
        std::vector<std::complex<float>> out;
        out.reserve(N / factor + 1);
        for (size_t i = 0; i < N; i += factor) {
            float sR = 0.0f, sI = 0.0f;
            for (size_t j = 0; j < T; j++) {
                int idx = static_cast<int>(i) - half + static_cast<int>(j);
                if (idx >= 0 && idx < static_cast<int>(N)) { sR += in[idx].real() * taps[j]; sI += in[idx].imag() * taps[j]; }
            }
            out.emplace_back(sR, sI);
        }
        return out;
    }

    static std::vector<std::complex<float>> applyComplexFIR(
        const std::vector<std::complex<float>>& in, const std::vector<float>& taps)
    {
        if (in.empty() || taps.empty()) return in;
        const size_t N = in.size(), T = taps.size();
        const int half = T / 2;
        std::vector<std::complex<float>> out(N);
        for (size_t i = 0; i < N; i++) {
            float sR = 0.0f, sI = 0.0f;
            for (size_t j = 0; j < T; j++) {
                int idx = static_cast<int>(i) - half + static_cast<int>(j);
                if (idx >= 0 && idx < static_cast<int>(N)) { sR += in[idx].real() * taps[j]; sI += in[idx].imag() * taps[j]; }
            }
            out[i] = {sR, sI};
        }
        return out;
    }

    static std::vector<float> decimateReal(const std::vector<float>& in, const std::vector<float>& taps, int factor)
    {
        const size_t N = in.size(), T = taps.size();
        const int half = T / 2;
        std::vector<float> out;
        out.reserve(N / factor + 1);
        for (size_t i = 0; i < N; i += factor) {
            float sum = 0.0f;
            for (size_t j = 0; j < T; j++) { int idx = static_cast<int>(i) - half + static_cast<int>(j); if (idx >= 0 && idx < static_cast<int>(N)) sum += in[idx] * taps[j]; }
            out.push_back(sum);
        }
        return out;
    }

    static std::vector<float> applyFIR(const std::vector<float>& in, const std::vector<float>& taps)
    {
        if (in.empty() || taps.empty()) return in;
        const size_t N = in.size(), T = taps.size();
        const int half = T / 2;
        std::vector<float> out(N);
        for (size_t i = 0; i < N; i++) {
            float sum = 0.0f;
            for (size_t j = 0; j < T; j++) { int idx = static_cast<int>(i) - half + static_cast<int>(j); if (idx >= 0 && idx < static_cast<int>(N)) sum += in[idx] * taps[j]; }
            out[i] = sum;
        }
        return out;
    }

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
            if (idx + 1 < in.size()) out.push_back(static_cast<float>(in[idx] * (1.0 - frac) + in[idx + 1] * frac));
            else if (idx < in.size()) out.push_back(in[idx]);
        }
        return out;
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
