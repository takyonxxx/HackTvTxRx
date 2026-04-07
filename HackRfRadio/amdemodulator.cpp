#include "amdemodulator.h"
#include <QDebug>

AMDemodulator::AMDemodulator(double inputSampleRate, double bandwidth, QObject *parent)
    : QObject(parent)
    , m_inputRate(inputSampleRate)
    , m_bandwidth(bandwidth)
{
    rebuildChain();
    qDebug() << "AMDemodulator: input" << m_inputRate / 1e6 << "MHz"
             << "bw" << m_bandwidth / 1e3 << "kHz";
}

std::vector<float> AMDemodulator::demodulate(const std::vector<std::complex<float>>& samples)
{
    if (samples.empty()) return {};

    auto iq = samples;
    for (auto& stage : m_iqStages) {
        std::vector<std::complex<float>> tmp;
        decimateComplex(iq, tmp, stage.taps, stage.factor, stage.iqHistory);
        iq = std::move(tmp);
    }

    if (!m_iqBandwidthTaps.empty()) {
        std::vector<std::complex<float>> tmp;
        applyComplexFIR(iq, tmp, m_iqBandwidthTaps, m_iqBwHistory);
        iq = std::move(tmp);
    }

    auto audio = amDemod(iq);

    for (auto& stage : m_realStages) {
        std::vector<float> tmp;
        decimateReal(audio, tmp, stage.taps, stage.factor, stage.realHistory);
        audio = std::move(tmp);
    }

    double lastRate = m_realStages.empty()
                          ? (m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate)
                          : m_realStages.back().outputRate;
    if (std::abs(lastRate - 48000.0) > 1.0) {
        audio = resample(audio, lastRate, 48000.0);
    }

    {
        std::vector<float> tmp;
        applyFIR(audio, tmp, m_audioFilterTaps, m_audioFilterHistory);
        audio = std::move(tmp);
    }

    // DC blocking is now handled inside amDemod (SDR++ style)
    return audio;
}

void AMDemodulator::setSampleRate(double newRate)
{
    m_inputRate = newRate;
    rebuildChain();
    qDebug() << "AMDemodulator: rate changed to" << m_inputRate / 1e6 << "MHz";
}

void AMDemodulator::setBandwidth(double bandwidthHz)
{
    m_bandwidth = std::clamp(bandwidthHz, 2000.0, 500000.0);
    double postDecimRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;
    float cutoff = static_cast<float>(std::min(m_bandwidth * 0.5, postDecimRate * 0.45));
    m_iqBandwidthTaps = designLPF(51, cutoff, static_cast<float>(postDecimRate));
    m_iqBwHistory.clear();
}

void AMDemodulator::rebuildChain()
{
    m_iqStages.clear();
    m_realStages.clear();
    m_iqBwHistory.clear();
    m_audioFilterHistory.clear();

    double rate = m_inputRate;
    double iqTarget = std::max(m_bandwidth * 6.0, 60000.0);
    iqTarget = std::min(iqTarget, 200000.0);
    const int candidates[] = {10, 8, 5, 4, 3, 2};

    while (rate > iqTarget * 1.5) {
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

    while (rate > 96000.0) {
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

    float audioCutoff = std::min(4000.0f, static_cast<float>(m_bandwidth / 2.0));
    m_audioFilterTaps = designLPF(31, audioCutoff, 48000.0f);

    // SDR++ style: LPF cutoff = bandwidth/2
    double postDecimRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;
    float defaultBW = static_cast<float>(std::min(m_bandwidth * 0.5, postDecimRate * 0.45));
    if (defaultBW > 0) {
        m_iqBandwidthTaps = designLPF(51, defaultBW, static_cast<float>(postDecimRate));
    } else {
        m_iqBandwidthTaps.clear();
    }
}

// AM Demodulation — SDR++ style
// 1. Envelope detection (magnitude)
// 2. DC blocker (adaptive, per-sample)
// 3. AGC with look-ahead clipping prevention
std::vector<float> AMDemodulator::amDemod(const std::vector<std::complex<float>>& signal)
{
    if (signal.empty()) return {};
    const size_t n = signal.size();
    std::vector<float> out(n);

    // SDR++ AGC parameters
    const float setPoint = 1.0f;
    const float attack = 0.001f;       // fast attack
    const float invAttack = 1.0f - attack;
    const float decay = 0.0001f;       // slow decay
    const float invDecay = 1.0f - decay;
    const float maxGain = 10000.0f;
    const float maxOutputAmp = 10.0f;

    // 1. Envelope detection
    for (size_t i = 0; i < n; i++) {
        out[i] = std::sqrt(signal[i].real() * signal[i].real() +
                           signal[i].imag() * signal[i].imag());
    }

    // 2. DC blocker (SDR++ style: offset += out * rate)
    const float dcRate = 0.0001f;  // slow tracking
    for (size_t i = 0; i < n; i++) {
        float clean = out[i] - m_dcOffset;
        m_dcOffset += clean * dcRate;
        out[i] = clean;
    }

    // 3. AGC with look-ahead (SDR++ style)
    for (size_t i = 0; i < n; i++) {
        float inAmp = std::fabs(out[i]);
        float gain;

        if (inAmp != 0.0f) {
            // Update amplitude estimate
            m_agcAmp = (inAmp > m_agcAmp)
                ? (m_agcAmp * invAttack + inAmp * attack)
                : (m_agcAmp * invDecay + inAmp * decay);
            gain = std::min(setPoint / m_agcAmp, maxGain);
        } else {
            gain = 1.0f;
        }

        // Look-ahead clipping prevention
        if (inAmp * gain > maxOutputAmp) {
            float maxAmp = 0.0f;
            for (size_t j = i; j < n; j++) {
                float a = std::fabs(out[j]);
                if (a > maxAmp) maxAmp = a;
            }
            m_agcAmp = maxAmp;
            gain = std::min(setPoint / m_agcAmp, maxGain);
        }

        out[i] *= gain;
    }

    return out;
}

std::vector<float> AMDemodulator::designLPF(int numTaps, float cutoff, float sampleRate)
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

// ========== Stateful FIR filters ==========

void AMDemodulator::decimateComplex(
    const std::vector<std::complex<float>>& in,
    std::vector<std::complex<float>>& out,
    const std::vector<float>& taps, int factor,
    std::vector<std::complex<float>>& history)
{
    const size_t T = taps.size();
    const size_t N = in.size();
    if (history.size() != T - 1) history.assign(T - 1, {0.0f, 0.0f});
    std::vector<std::complex<float>> work;
    work.reserve(history.size() + N);
    work.insert(work.end(), history.begin(), history.end());
    work.insert(work.end(), in.begin(), in.end());
    out.clear();
    out.reserve(N / factor + 1);
    for (size_t i = 0; i < N; i += factor) {
        float sumR = 0.0f, sumI = 0.0f;
        for (size_t j = 0; j < T; j++) {
            const auto& s = work[i + j];
            sumR += s.real() * taps[j];
            sumI += s.imag() * taps[j];
        }
        out.emplace_back(sumR, sumI);
    }
    if (N >= T - 1) {
        std::copy(in.end() - (T - 1), in.end(), history.begin());
    } else {
        size_t keep = (T - 1) - N;
        std::copy(history.end() - keep, history.end(), history.begin());
        std::copy(in.begin(), in.end(), history.begin() + keep);
    }
}

void AMDemodulator::applyComplexFIR(
    const std::vector<std::complex<float>>& in,
    std::vector<std::complex<float>>& out,
    const std::vector<float>& taps,
    std::vector<std::complex<float>>& history)
{
    if (in.empty() || taps.empty()) { out = in; return; }
    const size_t T = taps.size();
    const size_t N = in.size();
    if (history.size() != T - 1) history.assign(T - 1, {0.0f, 0.0f});
    std::vector<std::complex<float>> work;
    work.reserve(history.size() + N);
    work.insert(work.end(), history.begin(), history.end());
    work.insert(work.end(), in.begin(), in.end());
    out.resize(N);
    for (size_t i = 0; i < N; i++) {
        float sumR = 0.0f, sumI = 0.0f;
        for (size_t j = 0; j < T; j++) {
            const auto& s = work[i + j];
            sumR += s.real() * taps[j];
            sumI += s.imag() * taps[j];
        }
        out[i] = {sumR, sumI};
    }
    if (N >= T - 1) {
        std::copy(in.end() - (T - 1), in.end(), history.begin());
    } else {
        size_t keep = (T - 1) - N;
        std::copy(history.end() - keep, history.end(), history.begin());
        std::copy(in.begin(), in.end(), history.begin() + keep);
    }
}

void AMDemodulator::decimateReal(
    const std::vector<float>& in,
    std::vector<float>& out,
    const std::vector<float>& taps, int factor,
    std::vector<float>& history)
{
    const size_t T = taps.size();
    const size_t N = in.size();
    if (history.size() != T - 1) history.assign(T - 1, 0.0f);
    std::vector<float> work;
    work.reserve(history.size() + N);
    work.insert(work.end(), history.begin(), history.end());
    work.insert(work.end(), in.begin(), in.end());
    out.clear();
    out.reserve(N / factor + 1);
    for (size_t i = 0; i < N; i += factor) {
        float sum = 0.0f;
        for (size_t j = 0; j < T; j++) sum += work[i + j] * taps[j];
        out.push_back(sum);
    }
    if (N >= T - 1) {
        std::copy(in.end() - (T - 1), in.end(), history.begin());
    } else {
        size_t keep = (T - 1) - N;
        std::copy(history.end() - keep, history.end(), history.begin());
        std::copy(in.begin(), in.end(), history.begin() + keep);
    }
}

void AMDemodulator::applyFIR(
    const std::vector<float>& in,
    std::vector<float>& out,
    const std::vector<float>& taps,
    std::vector<float>& history)
{
    if (in.empty() || taps.empty()) { out = in; return; }
    const size_t T = taps.size();
    const size_t N = in.size();
    if (history.size() != T - 1) history.assign(T - 1, 0.0f);
    std::vector<float> work;
    work.reserve(history.size() + N);
    work.insert(work.end(), history.begin(), history.end());
    work.insert(work.end(), in.begin(), in.end());
    out.resize(N);
    for (size_t i = 0; i < N; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < T; j++) sum += work[i + j] * taps[j];
        out[i] = sum;
    }
    if (N >= T - 1) {
        std::copy(in.end() - (T - 1), in.end(), history.begin());
    } else {
        size_t keep = (T - 1) - N;
        std::copy(history.end() - keep, history.end(), history.begin());
        std::copy(in.begin(), in.end(), history.begin() + keep);
    }
}

std::vector<float> AMDemodulator::resample(const std::vector<float>& in, double inRate, double outRate)
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
