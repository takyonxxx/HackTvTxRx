#include "fmdemodulator.h"
#include <QDebug>

FMDemodulator::FMDemodulator(double inputSampleRate, double bandwidth, QObject *parent)
    : QObject(parent)
    , m_inputRate(inputSampleRate)
    , m_bandwidth(bandwidth)
    , m_lastPhase(0.0f)
    , m_outputGain(1.0f)
    , m_deemphTau(0.0f)
    , m_deemphPrev(0.0f)
{
    rebuildChain();

    qDebug() << "FMDemodulator: input" << m_inputRate / 1e6 << "MHz"
             << "bw" << m_bandwidth / 1e3 << "kHz"
             << "gain" << m_outputGain
             << "stages:" << m_iqStages.size() << "IQ +"
             << m_realStages.size() << "real";
}

std::vector<float> FMDemodulator::demodulate(const std::vector<std::complex<float>>& samples)
{
    if (samples.empty()) return {};

    // 1. Multi-stage complex IQ decimation
    auto iq = samples;
    for (const auto& stage : m_iqStages) {
        iq = decimateComplex(iq, stage.taps, stage.factor);
    }

    // 2. Adjustable IQ bandwidth filter
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

    // 7. High-pass filter at 250 Hz - removes low-freq rumble, improves voice clarity
    {
        // Single-pole HPF: y[n] = alpha * (y[n-1] + x[n] - x[n-1])
        float cutoff = 250.0f;
        float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * cutoff);
        float dt = 1.0f / 48000.0f;
        float alpha = rc / (rc + dt);
        for (auto& s : audio) {
            float filtered = alpha * (m_hpfPrev + s - m_hpfPrevIn);
            m_hpfPrevIn = s;
            m_hpfPrev = filtered;
            s = filtered;
        }
    }

    // 8. De-emphasis (user-controlled via DeEmph slider, 0 = off)
    if (m_deemphTau > 0.0f) {
        float dt = 1.0f / 48000.0f;
        float alphaD = m_deemphTau / (m_deemphTau + dt);
        for (auto& s : audio) {
            m_deemphPrev = alphaD * m_deemphPrev + (1.0f - alphaD) * s;
            s = m_deemphPrev;
        }
    }

    // 9. DC removal
    removeDC(audio);

    // 10. Soft limiter - prevents clipping distortion
    for (auto& s : audio) {
        if (s > 0.9f) s = 0.9f + 0.1f * std::tanh((s - 0.9f) * 8.0f);
        else if (s < -0.9f) s = -0.9f + 0.1f * std::tanh((s + 0.9f) * 8.0f);
    }

    return audio;
}

void FMDemodulator::setSampleRate(double newRate)
{
    m_inputRate = newRate;
    m_lastPhase = 0.0f;
    rebuildChain();
    qDebug() << "FMDemodulator: rate changed to" << m_inputRate / 1e6 << "MHz";
}

void FMDemodulator::setBandwidth(double bandwidthHz)
{
    m_bandwidth = std::clamp(bandwidthHz, 5000.0, 500000.0);
    rebuildChain();

    double postDecimRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;
    qDebug() << "FMDemodulator: BW" << m_bandwidth / 1e3 << "kHz, gain" << m_outputGain
             << "postDecim" << postDecimRate / 1e3 << "kHz";
}

void FMDemodulator::rebuildChain()
{
    m_iqStages.clear();
    m_realStages.clear();

    double rate = m_inputRate;

    bool isNBFM = (m_bandwidth <= 25000.0);

    // IQ decimation target:
    // NBFM (Yaesu FT-60 etc): +-5kHz deviation, 12.5kHz channel
    //   Need at least 10x deviation = 50kHz IF bandwidth for clean demod
    //   But keep enough headroom: target 200kHz for good phase tracking
    // WBFM: +-75kHz deviation, need ~300kHz
    double iqTarget;
    if (isNBFM) {
        iqTarget = 200000.0;  // 200kHz - gives clean FM demod for 5kHz deviation
    } else {
        iqTarget = std::max(m_bandwidth * 2.0, 300000.0);
    }
    iqTarget = std::min(iqTarget, 400000.0);

    const int candidates[] = {10, 8, 5, 4, 3, 2};

    while (rate > iqTarget * 2.0) {
        int best = 0;
        for (int f : candidates) {
            if (rate / f >= iqTarget) {
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

    // Phase 2: Real decimation - bring rate toward ~48 kHz
    while (rate > 96000.0) {
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

    // Audio filter and output gain
    if (isNBFM) {
        // NFM voice: 300Hz-3.5kHz passband
        // Use more taps for sharper cutoff to reject out-of-band noise
        m_audioFilterTaps = designLPF(51, 3500.0f, 48000.0f);
        if (m_outputGain <= 0.0f) {
            m_outputGain = 4.5f;
        }
    } else {
        // WBFM: 15 kHz audio bandwidth
        m_audioFilterTaps = designLPF(31, 15000.0f, 48000.0f);
        if (m_outputGain <= 0.0f) {
            m_outputGain = 0.5f;
        }
    }

    // IQ bandwidth filter at post-decimation rate
    double postDecimRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;

    float filterBW;
    if (isNBFM) {
        // NFM: use exactly the user-set bandwidth for IQ filtering
        // More taps = sharper filter = less adjacent channel interference
        filterBW = static_cast<float>(std::min(m_bandwidth, postDecimRate * 0.45));
    } else {
        filterBW = static_cast<float>(std::min(m_bandwidth, postDecimRate * 0.45));
    }

    int iqFilterTaps = isNBFM ? 51 : 31;  // more taps for NFM
    if (filterBW > 0) {
        m_iqBandwidthTaps = designLPF(iqFilterTaps, filterBW, static_cast<float>(postDecimRate));
    } else {
        m_iqBandwidthTaps.clear();
    }
}

// ========== FIR Filter Design (Hamming window) ==========
std::vector<float> FMDemodulator::designLPF(int numTaps, float cutoff, float sampleRate)
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

std::vector<std::complex<float>> FMDemodulator::decimateComplex(
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

std::vector<std::complex<float>> FMDemodulator::applyComplexFIR(
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

std::vector<float> FMDemodulator::decimateReal(
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

std::vector<float> FMDemodulator::applyFIR(const std::vector<float>& in, const std::vector<float>& taps)
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

// FM Demodulation with mode-aware gain
std::vector<float> FMDemodulator::fmDemod(const std::vector<std::complex<float>>& signal, double rate)
{
    if (signal.empty()) return {};

    std::vector<float> out(signal.size());

    // Conjugate multiply FM demodulation:
    // y[n] = angle(x[n] * conj(x[n-1]))
    // This is equivalent to phase difference but avoids atan2 discontinuities
    // and produces cleaner output with less phase noise.

    float rateNorm = static_cast<float>(rate) / (2.0f * static_cast<float>(M_PI));
    float refDeviation = (m_bandwidth <= 25000.0) ? 5000.0f : 75000.0f;
    float scale = (rateNorm / refDeviation) * m_outputGain * m_rxModIndex;

    // First sample uses stored previous sample
    std::complex<float> prevSample(std::cos(m_lastPhase), std::sin(m_lastPhase));

    for (size_t i = 0; i < signal.size(); i++) {
        // Conjugate multiply: product = x[n] * conj(x[n-1])
        std::complex<float> product = signal[i] * std::conj(prevSample);

        // Fast atan2 approximation for small angles (typical in FM demod)
        // For better accuracy, use full atan2
        float delta = std::atan2(product.imag(), product.real());

        out[i] = delta * scale;
        prevSample = signal[i];
    }

    // Store last phase for continuity
    m_lastPhase = std::atan2(prevSample.imag(), prevSample.real());

    return out;
}

std::vector<float> FMDemodulator::resample(const std::vector<float>& in, double inRate, double outRate)
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
            out.push_back(static_cast<float>(in[idx] * (1.0 - frac) + in[idx + 1] * frac));
        else if (idx < in.size())
            out.push_back(in[idx]);
    }
    return out;
}

void FMDemodulator::removeDC(std::vector<float>& audio)
{
    constexpr float alpha = 0.995f;
    for (auto& s : audio) {
        float y = s - m_dcX1 + alpha * m_dcY1;
        m_dcX1 = s;
        m_dcY1 = y;
        s = y;
    }
}
