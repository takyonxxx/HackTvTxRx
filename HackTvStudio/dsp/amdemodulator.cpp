#include "amdemodulator.h"
#include <QDebug>

AMDemodulator::AMDemodulator(double inputSampleRate, double bandwidth, QObject *parent)
    : QObject(parent)
    , m_inputRate(inputSampleRate)
    , m_bandwidth(bandwidth)
{
    rebuildChain();
}

std::vector<float> AMDemodulator::demodulate(const std::vector<std::complex<float>>& samples)
{
    if (samples.empty()) return {};

    auto iq = samples;

    // IQ decimation
    for (auto& stage : m_iqStages) {
        std::vector<std::complex<float>> tmp;
        decimateComplex(iq, tmp, stage.taps, stage.factor, stage.iqHistory);
        iq = std::move(tmp);
    }

    // IQ bandwidth filter
    if (!m_iqBandwidthTaps.empty()) {
        std::vector<std::complex<float>> tmp;
        applyComplexFIR(iq, tmp, m_iqBandwidthTaps, m_iqBwHistory);
        iq = std::move(tmp);
    }

    // AM demod: magnitude → AGC → DC block
    auto audio = amDemod(iq);

    // Real decimation to get closer to 48kHz
    for (auto& stage : m_realStages) {
        std::vector<float> tmp;
        decimateReal(audio, tmp, stage.taps, stage.factor, stage.realHistory);
        audio = std::move(tmp);
    }

    // Resample to 48kHz
    double lastRate = m_realStages.empty()
                          ? (m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate)
                          : m_realStages.back().outputRate;
    if (std::abs(lastRate - 48000.0) > 1.0) {
        audio = resample(audio, lastRate, 48000.0);
    }

    // Audio lowpass filter
    {
        std::vector<float> tmp;
        applyFIR(audio, tmp, m_audioFilterTaps, m_audioFilterHistory);
        audio = std::move(tmp);
    }

    // Output peak limiter
    for (size_t i = 0; i < audio.size(); i++) {
        float absVal = std::abs(audio[i]);
        m_audioRms += 0.0005f * (absVal - m_audioRms);
        float threshold = std::max(m_audioRms * 4.0f, 0.15f);
        if (absVal > threshold) {
            audio[i] = (audio[i] > 0) ? threshold : -threshold;
        }
    }

    return audio;
}

void AMDemodulator::setSampleRate(double newRate)
{
    m_inputRate = newRate;
    rebuildChain();
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

    // Reset demod state
    m_agcAmp = 0.0f;  // 0 = will auto-init from first chunk's actual level
    m_dcOffset = 0.0f;
    m_dcBlockerX = 0.0f;
    m_dcBlockerY = 0.0f;
    m_dcI = 0.0f;
    m_dcQ = 0.0f;
    m_audioRms = 0.05f;

    qDebug() << "AM rebuildChain: inputRate=" << m_inputRate << "bandwidth=" << m_bandwidth;

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

    float audioCutoff = std::min(3500.0f, static_cast<float>(m_bandwidth / 2.0));
    m_audioFilterTaps = designLPF(63, audioCutoff, 48000.0f);  // 63 taps for sharper cutoff

    // SDR++ style: LPF cutoff = bandwidth/2
    double postDecimRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;
    float defaultBW = static_cast<float>(std::min(m_bandwidth * 0.5, postDecimRate * 0.45));
    if (defaultBW > 0) {
        m_iqBandwidthTaps = designLPF(51, defaultBW, static_cast<float>(postDecimRate));
    } else {
        m_iqBandwidthTaps.clear();
    }

    qDebug() << "AM chain built: iqStages=" << m_iqStages.size()
             << "realStages=" << m_realStages.size()
             << "postDecimRate=" << postDecimRate
             << "iqBwCutoff=" << defaultBW
             << "audioCutoff=" << audioCutoff;
}

// AM Demodulation — v4
// Simplified: magnitude → peak-hold AGC → DC block
// The key problem was AGC instability on small chunks. Solution: use a very
// slow per-sample peak tracker that converges over hundreds of chunks, not one.
std::vector<float> AMDemodulator::amDemod(const std::vector<std::complex<float>>& signal)
{
    if (signal.empty()) return {};
    const size_t n = signal.size();
    std::vector<float> out(n);

    // Step 1: Magnitude (envelope detection) — NO IQ DC removal
    // IQ DC removal was destroying AM info in small chunks.
    // The BW filter already centers the signal; DC spike is outside passband.
    for (size_t i = 0; i < n; i++) {
        float I = signal[i].real();
        float Q = signal[i].imag();
        out[i] = std::sqrt(I * I + Q * Q);
    }

    // Step 2: Per-sample peak-hold AGC
    // Track carrier level with very slow attack/decay (sample-rate based).
    // At 66kHz post-decim rate, alpha=0.0001 → time constant = 10000 samples = 150ms
    // This is slow enough to not follow audio modulation (lowest audio = 300Hz = 3.3ms)
    // but fast enough to track signal level changes.
    const float agcAlpha = 0.0001f;

    // Auto-initialize on first use
    if (m_agcAmp < 1e-6f && n > 0) {
        // Calculate initial level from first chunk
        float sum = 0.0f;
        for (size_t i = 0; i < n; i++) sum += out[i];
        m_agcAmp = sum / static_cast<float>(n);
        if (m_agcAmp < 1e-6f) m_agcAmp = 0.01f;
    }

    for (size_t i = 0; i < n; i++) {
        // Slow exponential follower on magnitude
        m_agcAmp += agcAlpha * (out[i] - m_agcAmp);

        // Normalize so carrier sits at ~1.0
        float gain = (m_agcAmp > 1e-6f) ? (1.0f / m_agcAmp) : 1.0f;
        if (gain > 50.0f) gain = 50.0f;
        out[i] *= gain;
    }

    // Step 3: DC blocker — remove the carrier (now ~1.0), leaving audio modulation
    // y[n] = x[n] - x[n-1] + R * y[n-1]
    // R = 0.999 at 66kHz → HPF cutoff ~10Hz — well below 300Hz voice band
    const float R = 0.999f;
    for (size_t i = 0; i < n; i++) {
        float x = out[i];
        float y = x - m_dcBlockerX + R * m_dcBlockerY;
        m_dcBlockerX = x;
        m_dcBlockerY = y;
        out[i] = y;
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
