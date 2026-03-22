#include "AudioDemodulator.h"
#include <QElapsedTimer>
#include <QDebug>
#include <QThread>
#include <algorithm>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AudioDemodulator::AudioDemodulator(QObject *parent)
    : QObject(parent)
    , m_inputSampleRate(16000000.0)
    , m_audioCapable(true)
    , m_lastPhase(0.0f)
    , m_audioPhase(0.0)
    , m_audioGain(1.0f)
    , m_audioEnabled(true)
    , fmDeviation(FM_DEVIATION)
{
    m_audioBuffer.reserve(AUDIO_BUFFER_SIZE * 2);
    m_currentCarrierFreq = AUDIO_CARRIER;
    m_audioPhaseIncrement = -2.0 * M_PI * AUDIO_CARRIER / m_inputSampleRate;

    initFinalFilter();
    rebuildDecimationChain();

    qDebug() << "========================================";
    qDebug() << "AudioDemodulator initialized (PAL-B):";
    qDebug() << "  Input sample rate:" << m_inputSampleRate / 1e6 << "MHz";
    qDebug() << "  Audio carrier: 5.5 MHz";
    qDebug() << "  FM deviation: +/-50 kHz";
    qDebug() << "  Audio gain:" << m_audioGain;
    qDebug() << "========================================";
}

AudioDemodulator::~AudioDemodulator()
{
}

void AudioDemodulator::setAudioCarrierFreq(double freqHz)
{
    m_currentCarrierFreq = freqHz;
    m_audioPhaseIncrement = -2.0 * M_PI * freqHz / m_inputSampleRate;
    qDebug() << "AudioDemodulator: carrier updated to" << freqHz / 1e6 << "MHz";
}

void AudioDemodulator::initFinalFilter()
{
    // Audio bandwidth filter (15 kHz for PAL-B) at 48 kHz output
    m_audioFilterTaps = designLowPassFIR(FILTER_TAPS, 15000.0f, 48000.0f);
}

// ============================================================
// Dynamic Decimation Chain Builder
// ============================================================
//
// Goal: reduce m_inputSampleRate -> 48 kHz using integer decimation stages.
//
// Strategy: greedily pick the largest safe integer factor at each stage.
// "Safe" means the anti-alias filter cutoff is well within Nyquist.
// After all integer stages, do a final linear-interpolation resample to 48 kHz.
//
// Known good chains:
//   16 MHz: /5 -> 3.2M, /10 -> 320k, /2 -> 160k, /3 -> 53.3k, resample -> 48k
//   20 MHz: /5 -> 4M,   /10 -> 400k, /2 -> 200k, /4 -> 50k,   resample -> 48k
//   12.5M:  /5 -> 2.5M, /5  -> 500k, /2 -> 250k, /5 -> 50k,   resample -> 48k
//   10 MHz: /5 -> 2M,   /5  -> 400k, /2 -> 200k, /4 -> 50k,   resample -> 48k
//
// For rates where carrier >= Nyquist (e.g. 8 MHz: Nyquist=4 MHz < 5.5 MHz),
// audio is impossible. Set m_audioCapable = false.

void AudioDemodulator::rebuildDecimationChain()
{
    m_decimChain.clear();

    double nyquist = m_inputSampleRate / 2.0;

    // Check if audio carrier is representable
    // Use the actual carrier freq (may be offset-adjusted, e.g. 5.45 MHz instead of 5.5 MHz)
    if (m_currentCarrierFreq >= nyquist) {
        m_audioCapable = false;
        qDebug() << "AudioDemodulator: AUDIO NOT POSSIBLE at"
                 << m_inputSampleRate / 1e6 << "MHz"
                 << "(carrier" << m_currentCarrierFreq / 1e6 << "MHz >= Nyquist" << nyquist / 1e6 << "MHz)";
        emit audioCapabilityChanged(false, m_inputSampleRate, m_currentCarrierFreq);
        return;
    }

    m_audioCapable = true;

    // Build decimation stages: reduce rate from m_inputSampleRate toward ~50-60 kHz,
    // then final resample to exactly 48 kHz.
    double currentRate = m_inputSampleRate;

    // Target: get close to 48 kHz but not below it.
    // Minimum rate before final resample: 48 kHz (can't go below output rate).
    // Comfortable target: 48-80 kHz range before final resample.
    static constexpr double MIN_INTERMEDIATE = 48000.0;
    static constexpr double TARGET_INTERMEDIATE = 60000.0;

    // Candidate integer factors to try at each stage (largest first)
    static constexpr int candidateFactors[] = {10, 8, 5, 4, 3, 2};

    while (currentRate > TARGET_INTERMEDIATE * 2.0) {
        // Pick the largest factor that doesn't drop us below MIN_INTERMEDIATE
        int bestFactor = 0;
        for (int f : candidateFactors) {
            double newRate = currentRate / f;
            if (newRate >= MIN_INTERMEDIATE) {
                bestFactor = f;
                break;
            }
        }
        if (bestFactor < 2) break; // can't decimate further with integers

        double newRate = currentRate / bestFactor;

        // Anti-alias filter: cutoff at 0.4 * currentRate / bestFactor
        // (i.e. 80% of the new Nyquist)
        float cutoff = static_cast<float>(newRate * 0.4);
        int taps = FILTER_TAPS;
        // Use more taps for aggressive decimation
        if (bestFactor >= 8) taps = 33;
        else if (bestFactor >= 5) taps = 21;

        DecimStage stage;
        stage.filterTaps = designLowPassFIR(taps, cutoff, static_cast<float>(currentRate));
        stage.decimFactor = bestFactor;
        stage.outputRate = newRate;
        m_decimChain.push_back(std::move(stage));

        currentRate = newRate;
    }

    // Log the chain
    qDebug() << "AudioDemodulator: decimation chain for"
             << m_inputSampleRate / 1e6 << "MHz:";
    double rate = m_inputSampleRate;
    for (size_t i = 0; i < m_decimChain.size(); i++) {
        const auto& s = m_decimChain[i];
        qDebug() << "  Stage" << i << ":"
                 << rate / 1e3 << "kHz /" << s.decimFactor
                 << "->" << s.outputRate / 1e3 << "kHz"
                 << "(" << s.filterTaps.size() << "taps)";
        rate = s.outputRate;
    }
    qDebug() << "  Final resample:" << rate / 1e3 << "kHz -> 48 kHz";

    emit audioCapabilityChanged(true, m_inputSampleRate, m_currentCarrierFreq);
}

std::vector<float> AudioDemodulator::designLowPassFIR(
    int numTaps,
    float cutoffFreq,
    float sampleRate)
{
    std::vector<float> coeffs(numTaps);
    float fc = cutoffFreq / sampleRate;
    int center = numTaps / 2;

    for (int i = 0; i < numTaps; i++) {
        if (i == center) {
            coeffs[i] = 2.0f * fc;
        } else {
            float n = i - center;
            coeffs[i] = std::sin(2.0f * M_PI * fc * n) / (M_PI * n);
        }

        float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (numTaps - 1));
        coeffs[i] *= window;
    }

    float sum = std::accumulate(coeffs.begin(), coeffs.end(), 0.0f);
    if (sum != 0) {
        for (auto& c : coeffs) c /= sum;
    }

    return coeffs;
}

std::vector<float> AudioDemodulator::applyFIRFilter(
    const std::vector<float>& signal,
    const std::vector<float>& coeffs)
{
    if (signal.empty() || coeffs.empty()) {
        return std::vector<float>();
    }

    const size_t signalSize = signal.size();
    const size_t filterSize = coeffs.size();
    const int halfTaps = filterSize / 2;

    std::vector<float> filtered(signalSize);

    for (size_t i = 0; i < halfTaps && i < signalSize; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < filterSize; j++) {
            int idx = i - halfTaps + j;
            if (idx >= 0 && idx < static_cast<int>(signalSize)) {
                sum += signal[idx] * coeffs[j];
            }
        }
        filtered[i] = sum;
    }

    for (size_t i = halfTaps; i < signalSize - halfTaps; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < filterSize; j++) {
            sum += signal[i - halfTaps + j] * coeffs[j];
        }
        filtered[i] = sum;
    }

    for (size_t i = signalSize - halfTaps; i < signalSize; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < filterSize; j++) {
            int idx = i - halfTaps + j;
            if (idx >= 0 && idx < static_cast<int>(signalSize)) {
                sum += signal[idx] * coeffs[j];
            }
        }
        filtered[i] = sum;
    }

    return filtered;
}

std::vector<std::complex<float>> AudioDemodulator::frequencyShift(
    const std::vector<std::complex<float>>& signal,
    double shiftFreq)
{
    if (std::abs(shiftFreq) < 1.0) return signal;

    std::vector<std::complex<float>> shifted(signal.size());
    double phaseInc = 2.0 * M_PI * shiftFreq / m_inputSampleRate;
    double phase = m_audioPhase;

    for (size_t i = 0; i < signal.size(); i++) {
        std::complex<float> shift(std::cos(phase), std::sin(phase));
        shifted[i] = signal[i] * shift;
        phase += phaseInc;

        if (phase > M_PI) phase -= 2.0 * M_PI;
        if (phase < -M_PI) phase += 2.0 * M_PI;
    }

    m_audioPhase = phase;
    return shifted;
}

float AudioDemodulator::unwrapPhase(float phase, float lastPhase)
{
    float delta = phase - lastPhase;

    while (delta > M_PI) delta -= 2.0f * M_PI;
    while (delta < -M_PI) delta += 2.0f * M_PI;

    return delta;
}

std::vector<float> AudioDemodulator::fmDemodulateAtan2(
    const std::vector<std::complex<float>>& signal)
{
    if (signal.empty()) return std::vector<float>();

    std::vector<float> demod(signal.size());

    QMutexLocker phaseLock(&m_phaseMutex);
    float currentPhase = m_lastPhase;
    phaseLock.unlock();

    // FM demod scaling uses the input sample rate (signal is still at full rate here)
    // FM demod: output phase difference normalized so that +/-FM_DEVIATION maps to +/-1.0
    // At input sample rate, max instantaneous freq = deviation, so:
    //   delta_phase_max = 2*pi * deviation / sampleRate  (per sample)
    // We want output = delta / delta_max = delta * sampleRate / (2*pi * deviation)
    //
    // BUT this gives huge values at high sample rates (e.g. 160 at 16 MHz).
    // The correct normalization is simply delta / delta_max where delta_max
    // is the max phase step for the FM deviation at this sample rate.
    //
    // Actually the issue is: after decimation the effective rate drops.
    // The cleanest solution: just output raw phase difference (radians per sample)
    // and let the decimation + final gain handle amplitude.
    // Normalize so full deviation = +/-1.0
    float normFactor = static_cast<float>(m_inputSampleRate) / (2.0f * M_PI * fmDeviation);

    for (size_t i = 0; i < signal.size(); i++) {
        float phase = std::atan2(signal[i].imag(), signal[i].real());
        float delta = unwrapPhase(phase, currentPhase);

        demod[i] = delta * normFactor;

        currentPhase = phase;
    }

    phaseLock.relock();
    m_lastPhase = currentPhase;

    return demod;
}

std::vector<float> AudioDemodulator::fmDemodulateNarrowband(
    const std::vector<std::complex<float>>& signal,
    double signalRate)
{
    if (signal.empty()) return std::vector<float>();

    std::vector<float> demod(signal.size());

    QMutexLocker phaseLock(&m_phaseMutex);
    float currentPhase = m_lastPhase;
    phaseLock.unlock();

    // Output scaled phase difference.
    // Raw delta can peak at ~2 rad at 160 kHz narrowband.
    // Scale by 0.1 to bring into comfortable range (~0.1-0.2 peak).
    // User adjusts final level with Audio Gain slider.
    static constexpr float FM_OUTPUT_SCALE = 0.3f;

    for (size_t i = 0; i < signal.size(); i++) {
        float phase = std::atan2(signal[i].imag(), signal[i].real());
        float delta = unwrapPhase(phase, currentPhase);

        demod[i] = delta * FM_OUTPUT_SCALE;

        currentPhase = phase;
    }

    phaseLock.relock();
    m_lastPhase = currentPhase;

    return demod;
}

std::vector<float> AudioDemodulator::decimate(
    const std::vector<float>& signal,
    int factor)
{
    if (factor <= 1) return signal;

    std::vector<float> decimated;
    decimated.reserve(signal.size() / factor + 1);

    for (size_t i = 0; i < signal.size(); i += factor) {
        decimated.push_back(signal[i]);
    }

    return decimated;
}

// Simple linear interpolation resampler
std::vector<float> AudioDemodulator::resample(
    const std::vector<float>& signal,
    double inputRate,
    double outputRate)
{
    if (signal.empty() || inputRate <= 0 || outputRate <= 0) {
        return signal;
    }

    double ratio = inputRate / outputRate;
    size_t outputSize = static_cast<size_t>(signal.size() / ratio);
    std::vector<float> resampled;
    resampled.reserve(outputSize);

    for (size_t i = 0; i < outputSize; i++) {
        double srcPos = i * ratio;
        size_t idx = static_cast<size_t>(srcPos);

        if (idx + 1 < signal.size()) {
            double frac = srcPos - idx;
            float interpolated = signal[idx] * (1.0 - frac) + signal[idx + 1] * frac;
            resampled.push_back(interpolated);
        } else if (idx < signal.size()) {
            resampled.push_back(signal[idx]);
        }
    }

    return resampled;
}

void AudioDemodulator::emitAudioBuffer(const std::vector<float>& audio)
{
    if (audio.empty()) {
        return;
    }

    for (float sample : audio) {
        float processed = sample * m_audioGain;
        processed = std::clamp(processed, -1.0f, 1.0f);
        m_audioBuffer.push_back(processed);
    }

    static constexpr int CHUNK_SIZE = 480;  // 10ms @ 48kHz (match AudioOutput)

    while (m_audioBuffer.size() >= CHUNK_SIZE) {
        std::vector<float> chunk(m_audioBuffer.begin(),
                                 m_audioBuffer.begin() + CHUNK_SIZE);
        emit audioReady(chunk);

        m_audioBuffer.erase(m_audioBuffer.begin(),
                            m_audioBuffer.begin() + CHUNK_SIZE);
    }
}

void AudioDemodulator::processSamples(const int8_t* data, size_t len)
{
    if (!data || len == 0 || !m_audioEnabled) return;

    QMutexLocker locker(&m_processMutex);

    std::vector<std::complex<float>> samples;
    samples.reserve(len / 2);

    for (size_t i = 0; i < len; i += 2) {
        float I = static_cast<float>(data[i]) / 128.0f;
        float Q = static_cast<float>(data[i + 1]) / 128.0f;
        samples.emplace_back(I, Q);
    }

    processSamples(samples);
}

void AudioDemodulator::processSamples(const std::vector<std::complex<float>>& samples)
{
    if (!m_audioEnabled || samples.empty()) return;

    // If audio carrier is above Nyquist, skip processing entirely
    if (!m_audioCapable) return;

    try {
        // ============================================================
        // Correct FM audio demod pipeline:
        //   1. Freq shift audio carrier to baseband (complex)
        //   2. Narrowband filter + decimate (complex) to reduce bandwidth
        //      BEFORE FM demod — this is critical! FM demod on wideband
        //      signal produces huge values from video/noise.
        //   3. FM demodulate the narrowband signal
        //   4. Continue decimation (real) to reach ~50 kHz
        //   5. Resample to 48 kHz
        //   6. Final audio bandwidth filter
        // ============================================================

        // 1. Frequency shift audio carrier to baseband (complex IQ)
        auto iq = frequencyShift(samples, -m_currentCarrierFreq);

        // 2. Apply narrowband complex filter + decimate through the chain
        //    until rate is low enough for FM demod.
        //    We want to get to ~100-200 kHz before FM demod for clean results.
        //    Apply decimation stages on complex IQ data until rate <= 200 kHz or
        //    we've done at least the first aggressive stages.
        double currentRate = m_inputSampleRate;
        size_t stageIdx = 0;

        // Decimate complex IQ through early stages (high rate stages)
        // Stop when rate drops below 200 kHz — that's narrow enough for FM demod
        while (stageIdx < m_decimChain.size() && currentRate > 200000.0) {
            const auto& stage = m_decimChain[stageIdx];

            // Apply FIR filter to I and Q separately
            std::vector<float> realPart(iq.size());
            std::vector<float> imagPart(iq.size());
            for (size_t i = 0; i < iq.size(); i++) {
                realPart[i] = iq[i].real();
                imagPart[i] = iq[i].imag();
            }

            realPart = applyFIRFilter(realPart, stage.filterTaps);
            imagPart = applyFIRFilter(imagPart, stage.filterTaps);

            // Decimate
            std::vector<std::complex<float>> decimated;
            decimated.reserve(iq.size() / stage.decimFactor + 1);
            for (size_t i = 0; i < realPart.size(); i += stage.decimFactor) {
                decimated.emplace_back(realPart[i], imagPart[i]);
            }

            iq = std::move(decimated);
            currentRate = stage.outputRate;
            stageIdx++;
        }

        // 3. FM demodulate the narrowband signal
        //    Now the signal is at ~160 kHz or lower — FM demod will give clean output
        auto audio = fmDemodulateNarrowband(iq, currentRate);

        // 4. Continue remaining decimation stages (real-valued now)
        while (stageIdx < m_decimChain.size()) {
            const auto& stage = m_decimChain[stageIdx];
            audio = applyFIRFilter(audio, stage.filterTaps);
            audio = decimate(audio, stage.decimFactor);
            currentRate = stage.outputRate;
            stageIdx++;
        }

        // 5. Final resample to exactly 48 kHz
        if (std::abs(currentRate - 48000.0) > 1.0) {
            audio = resample(audio, currentRate, 48000.0);
        }

        // 6. Final audio bandwidth filter at 15 kHz
        audio = applyFIRFilter(audio, m_audioFilterTaps);

        // 7. Emit
        emitAudioBuffer(audio);

    } catch (const std::exception& e) {
        qCritical() << "Audio processing error:" << e.what();
    }
}

std::vector<float> AudioDemodulator::demodulateAudio(
    const std::vector<std::complex<float>>& samples)
{
    QMutexLocker lock(&m_mutex);

    if (samples.empty() || m_currentCarrierFreq <= 0 || !m_audioCapable) {
        return std::vector<float>();
    }

    // Same pipeline as processSamples: shift -> decimate complex -> FM demod -> decimate real
    auto iq = frequencyShift(samples, -m_currentCarrierFreq);

    double currentRate = m_inputSampleRate;
    size_t stageIdx = 0;

    // Decimate complex IQ until rate <= 200 kHz
    while (stageIdx < m_decimChain.size() && currentRate > 200000.0) {
        const auto& stage = m_decimChain[stageIdx];
        std::vector<float> realPart(iq.size()), imagPart(iq.size());
        for (size_t i = 0; i < iq.size(); i++) {
            realPart[i] = iq[i].real();
            imagPart[i] = iq[i].imag();
        }
        realPart = applyFIRFilter(realPart, stage.filterTaps);
        imagPart = applyFIRFilter(imagPart, stage.filterTaps);
        std::vector<std::complex<float>> decimated;
        decimated.reserve(iq.size() / stage.decimFactor + 1);
        for (size_t i = 0; i < realPart.size(); i += stage.decimFactor) {
            decimated.emplace_back(realPart[i], imagPart[i]);
        }
        iq = std::move(decimated);
        currentRate = stage.outputRate;
        stageIdx++;
    }

    auto audio = fmDemodulateNarrowband(iq, currentRate);

    while (stageIdx < m_decimChain.size()) {
        const auto& stage = m_decimChain[stageIdx];
        audio = applyFIRFilter(audio, stage.filterTaps);
        audio = decimate(audio, stage.decimFactor);
        currentRate = stage.outputRate;
        stageIdx++;
    }

    if (std::abs(currentRate - 48000.0) > 1.0) {
        audio = resample(audio, currentRate, 48000.0);
    }

    audio = applyFIRFilter(audio, m_audioFilterTaps);

    return audio;
}

double AudioDemodulator::getSampleRate() const
{
    return m_inputSampleRate;
}

void AudioDemodulator::setSampleRate(double newSampleRate)
{
    QMutexLocker locker(&m_processMutex);

    if (std::abs(newSampleRate - m_inputSampleRate) < 1.0) return;

    m_inputSampleRate = newSampleRate;

    // Recompute NCO phase increment for current carrier at new rate
    m_audioPhaseIncrement = -2.0 * M_PI * m_currentCarrierFreq / m_inputSampleRate;

    // Reset phase to avoid discontinuity
    m_audioPhase = 0.0;
    m_lastPhase = 0.0f;

    // Clear audio buffer to avoid stale samples from old rate
    m_audioBuffer.clear();

    // Rebuild the entire decimation chain for the new rate
    rebuildDecimationChain();

    qDebug() << "AudioDemodulator::setSampleRate:" << m_inputSampleRate / 1e6 << "MHz"
             << "capable:" << m_audioCapable
             << "stages:" << m_decimChain.size();
}
