#include "fmdemodulator.h"
#include <QDebug>

FMDemodulator::FMDemodulator(double inputSampleRate, double bandwidth, QObject *parent)
    : QObject(parent)
    , m_inputRate(inputSampleRate)
    , m_bandwidth(bandwidth)
    , m_lastPhase(0.0f)
    , m_outputGain(1.0f)
    , m_deemphTau(0.0f)
    , m_deemphPrevL(0.0f)
    , m_deemphPrevR(0.0f)
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

    bool isWBFM = (m_bandwidth > 25000.0);

    // 1. Multi-stage complex IQ decimation
    auto iq = samples;
    for (auto& stage : m_iqStages) {
        std::vector<std::complex<float>> tmp;
        decimateComplex(iq, tmp, stage.taps, stage.factor, stage.iqHistory);
        iq = std::move(tmp);
    }

    // 2. Adjustable IQ bandwidth filter
    if (!m_iqBandwidthTaps.empty()) {
        std::vector<std::complex<float>> tmp;
        applyComplexFIR(iq, tmp, m_iqBandwidthTaps, m_iqBwHistory);
        iq = std::move(tmp);
    }

    // 2b. FM IF Noise Reduction (SDR++ FMNR)
    // Isolates the FM carrier's instantaneous frequency, strips all noise
    if (m_fmnrEnabled && !isWBFM) {
        applyFMNR(iq);
    }

    // 3. FM demodulate — produces MPX baseband
    double fmRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;
    auto mpx = fmDemod(iq, fmRate);

    // WBFM: Stereo decode at MPX rate (must be >= 76 kHz for 38 kHz subcarrier)
    if (isWBFM && fmRate >= 76000.0) {
        return decodeStereo(mpx, fmRate);
    }

    // NBFM / fallback mono path
    // 4. Real-valued decimation stages
    for (auto& stage : m_realStages) {
        std::vector<float> tmp;
        decimateReal(mpx, tmp, stage.taps, stage.factor, stage.realHistory);
        mpx = std::move(tmp);
    }

    // 5. Resample to 48 kHz
    double lastRate = m_realStages.empty() ? fmRate : m_realStages.back().outputRate;
    if (std::abs(lastRate - 48000.0) > 1.0) {
        mpx = resample(mpx, lastRate, 48000.0);
    }

    // 6. De-emphasis (BEFORE audio LPF — GNU Radio standard for NFM)
    if (m_deemphTau > 0.0f) {
        float dt = 1.0f / 48000.0f;
        float alphaD = m_deemphTau / (m_deemphTau + dt);
        for (auto& s : mpx) {
            m_deemphPrevL = alphaD * m_deemphPrevL + (1.0f - alphaD) * s;
            s = m_deemphPrevL;
        }
    }

    // 7-8. Audio LPF (controlled by slider) — HPF skipped (was making sound muffled)
    {
        std::vector<float> tmp;
        applyFIR(mpx, tmp, m_audioFilterTaps, m_audioFilterHistory);
        mpx = std::move(tmp);
    }

    // 9. DC removal
    removeDC(m_dcX1L, m_dcY1L, mpx);

    // 10. Volume gain + Soft limiter + mono→stereo interleave
    // outputGain is applied here as volume control, NOT in fmDemod
    std::vector<float> stereoOut(mpx.size() * 2);
    for (size_t i = 0; i < mpx.size(); i++) {
        float s = mpx[i] * m_outputGain;
        if (s > 0.9f) s = 0.9f + 0.1f * std::tanh((s - 0.9f) * 8.0f);
        else if (s < -0.9f) s = -0.9f + 0.1f * std::tanh((s + 0.9f) * 8.0f);
        stereoOut[i * 2]     = s;  // L
        stereoOut[i * 2 + 1] = s;  // R (mono duplicate)
    }

    if (m_stereoDetected.load()) {
        m_stereoDetected.store(false);
        emit stereoStatusChanged(false);
    }

    return stereoOut;
}

// ========== WBFM Stereo Decode ==========
// MPX signal at high rate (typically 400 kHz):
//   0-15 kHz:  L+R (mono)
//   19 kHz:    Pilot tone
//   23-53 kHz: L-R on 38 kHz DSB-SC subcarrier
//
// Steps:
//   1. Extract L+R with 15 kHz LPF
//   2. Detect 19 kHz pilot via PLL
//   3. Generate 38 kHz (2x pilot) reference
//   4. Multiply MPX by 38 kHz ref → demodulate L-R
//   5. LPF L-R at 15 kHz
//   6. L = L+R + L-R, R = L+R - L-R
//   7. Resample both to 48 kHz
//   8. De-emphasis, HPF, DC removal, interleave

std::vector<float> FMDemodulator::decodeStereo(const std::vector<float>& mpx, double mpxRate)
{
    const size_t N = mpx.size();

    // --- Pilot tone detection via Goertzel at 19 kHz ---
    float pilotEnergy = 0.0f;
    {
        // Goertzel algorithm for 19 kHz
        float k = 19000.0f / static_cast<float>(mpxRate) * N;
        float w = 2.0f * static_cast<float>(M_PI) * k / N;
        float coeff = 2.0f * std::cos(w);
        float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;
        for (size_t i = 0; i < N; i++) {
            s0 = mpx[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        float power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        pilotEnergy = power / (N * N);
    }

    // Smooth pilot level for stable detection
    m_pilotLevel = 0.8f * m_pilotLevel + 0.2f * pilotEnergy;

    bool stereoNow = (m_pilotLevel > 1e-8f);

    if (stereoNow != m_stereoDetected.load()) {
        m_stereoDetected.store(stereoNow);
        emit stereoStatusChanged(stereoNow);
    }

    // Force mono overrides stereo decode
    bool doStereo = stereoNow && !m_forceMono;

    // --- Extract L+R (mono) with 15 kHz LPF ---
    std::vector<float> monoSignal;
    applyFIR(mpx, monoSignal, m_monoFilterTaps, m_monoFilterHistory);

    std::vector<float> leftAudio, rightAudio;

    if (doStereo) {
        // --- PLL-based 38 kHz carrier recovery ---
        // Multiply MPX by 2x pilot (38 kHz) to demodulate L-R
        std::vector<float> diffRaw(N);
        double phaseInc = 2.0 * M_PI * 38000.0 / mpxRate;

        for (size_t i = 0; i < N; i++) {
            // Generate 38 kHz reference (2x pilot frequency)
            float ref = static_cast<float>(std::cos(m_pilotPhase * 2.0));
            diffRaw[i] = mpx[i] * ref * 2.0f;  // x2 to compensate DSB-SC

            // Simple PLL: track pilot at 19 kHz
            float pilotRef = static_cast<float>(std::sin(m_pilotPhase));
            float pilotError = mpx[i] * pilotRef;

            // PLL loop filter (narrow bandwidth for stable lock)
            double loopBw = 50.0;  // Hz
            double alpha = loopBw / mpxRate;
            m_pilotFreq += alpha * pilotError * 0.1;
            m_pilotFreq = std::clamp(m_pilotFreq, 18900.0, 19100.0);

            m_pilotPhase += 2.0 * M_PI * m_pilotFreq / mpxRate;
            if (m_pilotPhase > 2.0 * M_PI) m_pilotPhase -= 2.0 * M_PI;
        }

        // LPF the L-R signal at 15 kHz
        std::vector<float> diffSignal;
        applyFIR(diffRaw, diffSignal, m_diffFilterTaps, m_diffFilterHistory);

        // L = (L+R) + (L-R), R = (L+R) - (L-R)
        size_t len = std::min(monoSignal.size(), diffSignal.size());
        leftAudio.resize(len);
        rightAudio.resize(len);
        for (size_t i = 0; i < len; i++) {
            leftAudio[i]  = monoSignal[i] + diffSignal[i];
            rightAudio[i] = monoSignal[i] - diffSignal[i];
        }
    } else {
        // No stereo — mono duplicate
        leftAudio = monoSignal;
        rightAudio = monoSignal;
    }

    // --- Resample L and R to 48 kHz ---
    if (std::abs(mpxRate - 48000.0) > 1.0) {
        leftAudio = resample(leftAudio, mpxRate, 48000.0);
        rightAudio = resample(rightAudio, mpxRate, 48000.0);
    }

    // --- De-emphasis (per channel) ---
    if (m_deemphTau > 0.0f) {
        float dt = 1.0f / 48000.0f;
        float alphaD = m_deemphTau / (m_deemphTau + dt);
        for (auto& s : leftAudio) {
            m_deemphPrevL = alphaD * m_deemphPrevL + (1.0f - alphaD) * s;
            s = m_deemphPrevL;
        }
        for (auto& s : rightAudio) {
            m_deemphPrevR = alphaD * m_deemphPrevR + (1.0f - alphaD) * s;
            s = m_deemphPrevR;
        }
    }

    // --- HPF 250 Hz (per channel) ---
    {
        float cutoff = 250.0f;
        float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * cutoff);
        float dt = 1.0f / 48000.0f;
        float alpha = rc / (rc + dt);
        for (auto& s : leftAudio) {
            float f = alpha * (m_hpfPrevL + s - m_hpfPrevInL);
            m_hpfPrevInL = s; m_hpfPrevL = f; s = f;
        }
        for (auto& s : rightAudio) {
            float f = alpha * (m_hpfPrevR + s - m_hpfPrevInR);
            m_hpfPrevInR = s; m_hpfPrevR = f; s = f;
        }
    }

    // --- DC removal (per channel) ---
    removeDC(m_dcX1L, m_dcY1L, leftAudio);
    removeDC(m_dcX1R, m_dcY1R, rightAudio);

    // --- Interleave L/R + volume gain + soft limiter ---
    size_t outLen = std::min(leftAudio.size(), rightAudio.size());
    std::vector<float> stereoOut(outLen * 2);
    for (size_t i = 0; i < outLen; i++) {
        float l = leftAudio[i] * m_outputGain;
        float r = rightAudio[i] * m_outputGain;
        // Soft limiter
        if (l > 0.9f) l = 0.9f + 0.1f * std::tanh((l - 0.9f) * 8.0f);
        else if (l < -0.9f) l = -0.9f + 0.1f * std::tanh((l + 0.9f) * 8.0f);
        if (r > 0.9f) r = 0.9f + 0.1f * std::tanh((r - 0.9f) * 8.0f);
        else if (r < -0.9f) r = -0.9f + 0.1f * std::tanh((r + 0.9f) * 8.0f);
        stereoOut[i * 2]     = l;
        stereoOut[i * 2 + 1] = r;
    }

    return stereoOut;
}

void FMDemodulator::setSampleRate(double newRate)
{
    m_inputRate = newRate;
    m_lastPhase = 0.0f;
    m_pilotPhase = 0.0;
    m_pilotFreq = 19000.0;
    m_pilotLevel = 0.0f;
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

void FMDemodulator::setAudioLPF(float cutoffHz)
{
    float newCutoff = std::clamp(cutoffHz, 1000.0f, 8000.0f);
    if (std::abs(newCutoff - m_audioLpfCutoff) < 50.0f) return;  // ignore tiny changes
    m_audioLpfCutoff = newCutoff;
    bool isNBFM = (m_bandwidth <= 25000.0);
    if (isNBFM) {
        m_audioFilterTaps = designLPF(31, m_audioLpfCutoff, 48000.0f);
        // Don't clear history — causes clicks during slider drag
    }
}

void FMDemodulator::rebuildChain()
{
    m_iqStages.clear();
    m_realStages.clear();

    // Clear all persistent filter state
    m_iqBwHistory.clear();
    m_audioFilterHistory.clear();
    m_monoFilterHistory.clear();
    m_diffFilterHistory.clear();
    m_fmnrBuffer.clear();
    m_fmnrWindow.clear();

    double rate = m_inputRate;
    bool isNBFM = (m_bandwidth <= 25000.0);
    bool isWBFM = !isNBFM;

    // IQ decimation target:
    // NBFM: target 50 kHz — aggressive decimation removes most noise
    //        before FM demod, dramatically improving SNR
    // WBFM: target 400 kHz (must be >= 76 kHz for stereo decode at 38 kHz)
    double iqTarget;
    if (isNBFM) {
        // For 12.5 kHz channel: 50 kHz gives 4x oversampling, plenty for FM demod
        iqTarget = std::max(m_bandwidth * 4.0, 50000.0);
        iqTarget = std::min(iqTarget, 200000.0);
    } else {
        iqTarget = std::max(m_bandwidth * 2.0, 300000.0);
        iqTarget = std::min(iqTarget, 400000.0);
    }

    const int candidates[] = {10, 8, 5, 4, 3, 2};

    while (rate > iqTarget * 1.5) {
        int best = 0;
        for (int f : candidates) {
            if (rate / f >= iqTarget) { best = f; break; }
        }
        if (best < 2) break;

        double newRate = rate / best;
        // Tighter cutoff for better noise rejection
        float cutoff = static_cast<float>(newRate * 0.4);
        int taps = (best >= 8) ? 33 : (best >= 5) ? 21 : 17;

        DecimStage s;
        s.taps = designLPF(taps, cutoff, static_cast<float>(rate));
        s.factor = best;
        s.outputRate = newRate;
        m_iqStages.push_back(std::move(s));
        rate = newRate;
    }

    m_mpxRate = rate;  // store for stereo decode

    if (isWBFM) {
        // WBFM: NO real decimation here — stereo decode needs high rate
        m_monoFilterTaps = designLPF(63, 15000.0f, static_cast<float>(rate));
        m_diffFilterTaps = designLPF(63, 15000.0f, static_cast<float>(rate));
        m_audioFilterTaps = designLPF(31, 15000.0f, 48000.0f);
        if (m_outputGain <= 0.0f) m_outputGain = 0.5f;
    } else {
        // NBFM: real decimation to ~48 kHz (no stereo)
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

        // Voice audio filter — cutoff controlled by setAudioLPF / slider
        m_audioFilterTaps = designLPF(31, m_audioLpfCutoff, 48000.0f);
        if (m_outputGain <= 0.0f) m_outputGain = 4.5f;

        m_monoFilterTaps.clear();
        m_diffFilterTaps.clear();
    }

    // IQ bandwidth filter — applied after decimation, before FM demod
    double postDecimRate = m_iqStages.empty() ? m_inputRate : m_iqStages.back().outputRate;
    float filterBW = static_cast<float>(std::min(m_bandwidth, postDecimRate * 0.45));
    int iqFilterTaps = isNBFM ? 71 : 31;
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

// ========== Stateful FIR filters with persistent delay lines ==========
// Each filter maintains a history buffer (size = taps-1) that carries
// the tail of the previous block into the next. This eliminates the
// clicks/pops/noise that occurred at block boundaries with zero-padding.

void FMDemodulator::decimateComplex(
    const std::vector<std::complex<float>>& in,
    std::vector<std::complex<float>>& out,
    const std::vector<float>& taps, int factor,
    std::vector<std::complex<float>>& history)
{
    const size_t T = taps.size();
    const size_t N = in.size();

    // Initialize history if needed
    if (history.size() != T - 1)
        history.assign(T - 1, {0.0f, 0.0f});

    // Build working buffer: [history | input]
    std::vector<std::complex<float>> work;
    work.reserve(history.size() + N);
    work.insert(work.end(), history.begin(), history.end());
    work.insert(work.end(), in.begin(), in.end());

    out.clear();
    out.reserve(N / factor + 1);

    for (size_t i = 0; i < N; i += factor) {
        float sumR = 0.0f, sumI = 0.0f;
        // i in input maps to i + (T-1) in work buffer, filter centered
        for (size_t j = 0; j < T; j++) {
            const auto& s = work[i + j];
            sumR += s.real() * taps[j];
            sumI += s.imag() * taps[j];
        }
        out.emplace_back(sumR, sumI);
    }

    // Save last T-1 input samples as history for next block
    if (N >= T - 1) {
        std::copy(in.end() - (T - 1), in.end(), history.begin());
    } else {
        // Input shorter than history: shift old history and append input
        size_t keep = (T - 1) - N;
        std::copy(history.end() - keep, history.end(), history.begin());
        std::copy(in.begin(), in.end(), history.begin() + keep);
    }
}

void FMDemodulator::applyComplexFIR(
    const std::vector<std::complex<float>>& in,
    std::vector<std::complex<float>>& out,
    const std::vector<float>& taps,
    std::vector<std::complex<float>>& history)
{
    if (in.empty() || taps.empty()) { out = in; return; }

    const size_t T = taps.size();
    const size_t N = in.size();

    if (history.size() != T - 1)
        history.assign(T - 1, {0.0f, 0.0f});

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

void FMDemodulator::decimateReal(
    const std::vector<float>& in,
    std::vector<float>& out,
    const std::vector<float>& taps, int factor,
    std::vector<float>& history)
{
    const size_t T = taps.size();
    const size_t N = in.size();

    if (history.size() != T - 1)
        history.assign(T - 1, 0.0f);

    std::vector<float> work;
    work.reserve(history.size() + N);
    work.insert(work.end(), history.begin(), history.end());
    work.insert(work.end(), in.begin(), in.end());

    out.clear();
    out.reserve(N / factor + 1);

    for (size_t i = 0; i < N; i += factor) {
        float sum = 0.0f;
        for (size_t j = 0; j < T; j++) {
            sum += work[i + j] * taps[j];
        }
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

void FMDemodulator::applyFIR(
    const std::vector<float>& in,
    std::vector<float>& out,
    const std::vector<float>& taps,
    std::vector<float>& history)
{
    if (in.empty() || taps.empty()) { out = in; return; }

    const size_t T = taps.size();
    const size_t N = in.size();

    if (history.size() != T - 1)
        history.assign(T - 1, 0.0f);

    std::vector<float> work;
    work.reserve(history.size() + N);
    work.insert(work.end(), history.begin(), history.end());
    work.insert(work.end(), in.begin(), in.end());

    out.resize(N);
    for (size_t i = 0; i < N; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < T; j++) {
            sum += work[i + j] * taps[j];
        }
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

// FM Demodulation — SDR++/GNU Radio Quadrature Demod approach
// gain = sampleRate / (2π × deviation)
// For NBFM 12.5kHz BW at 50kHz rate: deviation = 6250, gain = 1.273
std::vector<float> FMDemodulator::fmDemod(const std::vector<std::complex<float>>& signal, double rate)
{
    if (signal.empty()) return {};

    std::vector<float> out(signal.size());

    bool isNBFM = (m_bandwidth <= 25000.0);

    // SDR++ Quadrature demod: gain = sampleRate / (2π × deviation)
    // deviation = bandwidth/2 for NBFM, 75kHz for WFM
    // rxModIndex multiplies deviation — higher = less gain = cleaner sound
    float deviation = isNBFM ? static_cast<float>(m_bandwidth * 0.5) : 75000.0f;
    deviation *= m_rxModIndex;
    float deviationRads = 2.0f * static_cast<float>(M_PI) * deviation / static_cast<float>(rate);
    float gain = 1.0f / deviationRads;

    for (size_t i = 0; i < signal.size(); i++) {
        float cphase = std::atan2(signal[i].imag(), signal[i].real());
        float delta = cphase - m_lastPhase;

        if (delta > static_cast<float>(M_PI)) delta -= 2.0f * static_cast<float>(M_PI);
        if (delta < -static_cast<float>(M_PI)) delta += 2.0f * static_cast<float>(M_PI);

        out[i] = delta * gain;
        m_lastPhase = cphase;
    }

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

void FMDemodulator::removeDC(float& dcX1, float& dcY1, std::vector<float>& audio)
{
    constexpr float alpha = 0.995f;
    for (auto& s : audio) {
        float y = s - dcX1 + alpha * dcY1;
        dcX1 = s;
        dcY1 = y;
        s = y;
    }
}

// ========== In-place radix-2 FFT (Cooley-Tukey) ==========
void FMDemodulator::fftInPlace(std::vector<std::complex<float>>& x, bool inverse)
{
    const size_t N = x.size();
    if (N <= 1) return;

    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < N; i++) {
        size_t bit = N >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    // Butterfly stages
    float sign = inverse ? 1.0f : -1.0f;
    for (size_t len = 2; len <= N; len <<= 1) {
        float ang = sign * 2.0f * static_cast<float>(M_PI) / len;
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < N; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; j++) {
                std::complex<float> u = x[i + j];
                std::complex<float> v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    // Normalize IFFT
    if (inverse) {
        float invN = 1.0f / static_cast<float>(N);
        for (auto& s : x) s *= invN;
    }
}

// ========== FM IF Noise Reduction (SDR++ FMNR style) ==========
// For each IQ sample: take a _bins-point FFT, keep the strongest bin
// plus immediate neighbors for smooth transitions, zero the rest, IFFT back.
void FMDemodulator::applyFMNR(std::vector<std::complex<float>>& iq)
{
    if (iq.empty()) return;

    const int bins = m_fmnrBins;

    // Initialize buffer and window on first call or if bins changed
    if (static_cast<int>(m_fmnrWindow.size()) != bins) {
        m_fmnrBuffer.assign(bins - 1, {0.0f, 0.0f});
        m_fmnrWindow.resize(bins);
        // Nuttall window
        for (int i = 0; i < bins; i++) {
            float x = 2.0f * static_cast<float>(M_PI) * i / (bins - 1);
            m_fmnrWindow[i] = 0.355768f - 0.487396f * std::cos(x)
                            + 0.144232f * std::cos(2.0f * x)
                            - 0.012604f * std::cos(3.0f * x);
        }
    }

    // Append input to delay buffer
    std::vector<std::complex<float>> work;
    work.reserve(m_fmnrBuffer.size() + iq.size());
    work.insert(work.end(), m_fmnrBuffer.begin(), m_fmnrBuffer.end());
    work.insert(work.end(), iq.begin(), iq.end());

    std::vector<std::complex<float>> fftBuf(bins);

    // Process each sample
    for (size_t i = 0; i < iq.size(); i++) {
        // Apply window to bins-length segment
        for (int j = 0; j < bins; j++) {
            fftBuf[j] = work[i + j] * m_fmnrWindow[j];
        }

        // Forward FFT
        fftInPlace(fftBuf, false);

        // Find bin with maximum amplitude
        int maxIdx = 0;
        float maxAmp = 0.0f;
        for (int j = 0; j < bins; j++) {
            float amp = fftBuf[j].real() * fftBuf[j].real() + fftBuf[j].imag() * fftBuf[j].imag();
            if (amp > maxAmp) { maxAmp = amp; maxIdx = j; }
        }

        // Keep only strongest bin (SDR++ original — no neighbors)
        std::vector<std::complex<float>> ifftBuf(bins, {0.0f, 0.0f});
        ifftBuf[maxIdx] = fftBuf[maxIdx];

        // Inverse FFT
        fftInPlace(ifftBuf, true);

        // Take center sample
        iq[i] = ifftBuf[bins / 2];
    }

    // Save last bins-1 samples as delay for next block
    size_t iqSize = iq.size();
    size_t histSize = bins - 1;
    if (iqSize >= histSize) {
        m_fmnrBuffer.assign(iq.end() - histSize, iq.end());
    } else {
        size_t keep = histSize - iqSize;
        m_fmnrBuffer.erase(m_fmnrBuffer.begin(), m_fmnrBuffer.begin() + (m_fmnrBuffer.size() - keep));
        m_fmnrBuffer.insert(m_fmnrBuffer.end(), iq.begin(), iq.end());
    }
}
