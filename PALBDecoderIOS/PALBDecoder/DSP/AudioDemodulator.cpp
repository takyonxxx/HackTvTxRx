#include "AudioDemodulator.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AudioDemodulator::AudioDemodulator()
    : m_inputSampleRate(16000000.0)
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
}

void AudioDemodulator::setAudioCarrierFreq(double freqHz)
{
    m_currentCarrierFreq = freqHz;
    m_audioPhaseIncrement = -2.0 * M_PI * freqHz / m_inputSampleRate;
}

void AudioDemodulator::initFinalFilter()
{
    m_audioFilterTaps = designLowPassFIR(FILTER_TAPS, 15000.0f, 48000.0f);
}

void AudioDemodulator::rebuildDecimationChain()
{
    m_decimChain.clear();
    double nyquist = m_inputSampleRate / 2.0;
    if (m_currentCarrierFreq >= nyquist) { m_audioCapable = false; return; }
    m_audioCapable = true;

    double currentRate = m_inputSampleRate;
    static constexpr double MIN_INTERMEDIATE = 48000.0;
    static constexpr double TARGET_INTERMEDIATE = 60000.0;
    static constexpr int candidateFactors[] = {10, 8, 5, 4, 3, 2};

    while (currentRate > TARGET_INTERMEDIATE * 2.0) {
        int bestFactor = 0;
        for (int f : candidateFactors) {
            double newRate = currentRate / f;
            if (newRate >= MIN_INTERMEDIATE) { bestFactor = f; break; }
        }
        if (bestFactor < 2) break;
        double newRate = currentRate / bestFactor;
        float cutoff = static_cast<float>(newRate * 0.4);
        int taps = FILTER_TAPS;
        if (bestFactor >= 8) taps = 33;
        else if (bestFactor >= 5) taps = 21;

        DecimStage stage;
        stage.filterTaps = designLowPassFIR(taps, cutoff, static_cast<float>(currentRate));
        stage.decimFactor = bestFactor;
        stage.outputRate = newRate;
        m_decimChain.push_back(std::move(stage));
        currentRate = newRate;
    }
}

std::vector<float> AudioDemodulator::designLowPassFIR(int numTaps, float cutoffFreq, float sampleRate)
{
    std::vector<float> coeffs(numTaps);
    float fc = cutoffFreq / sampleRate;
    int center = numTaps / 2;
    for (int i = 0; i < numTaps; i++) {
        if (i == center) coeffs[i] = 2.0f * fc;
        else {
            float n = i - center;
            coeffs[i] = std::sin(2.0f * M_PI * fc * n) / (M_PI * n);
        }
        float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (numTaps - 1));
        coeffs[i] *= window;
    }
    float sum = std::accumulate(coeffs.begin(), coeffs.end(), 0.0f);
    if (sum != 0) for (auto& c : coeffs) c /= sum;
    return coeffs;
}

std::vector<float> AudioDemodulator::applyFIRFilter(const std::vector<float>& signal, const std::vector<float>& coeffs)
{
    if (signal.empty() || coeffs.empty()) return {};
    const size_t signalSize = signal.size();
    const size_t filterSize = coeffs.size();
    const int halfTaps = filterSize / 2;
    std::vector<float> filtered(signalSize);

    for (size_t i = 0; i < signalSize; i++) {
        float sum = 0.0f;
        for (size_t j = 0; j < filterSize; j++) {
            int idx = (int)i - halfTaps + (int)j;
            if (idx >= 0 && idx < (int)signalSize) sum += signal[idx] * coeffs[j];
        }
        filtered[i] = sum;
    }
    return filtered;
}

std::vector<std::complex<float>> AudioDemodulator::frequencyShift(
    const std::vector<std::complex<float>>& signal, double shiftFreq)
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

std::vector<float> AudioDemodulator::fmDemodulateNarrowband(
    const std::vector<std::complex<float>>& signal, double signalRate)
{
    if (signal.empty()) return {};
    std::vector<float> demod(signal.size());
    std::lock_guard<std::mutex> lock(m_phaseMutex);
    float currentPhase = m_lastPhase;
    static constexpr float FM_OUTPUT_SCALE = 0.3f;
    for (size_t i = 0; i < signal.size(); i++) {
        float phase = std::atan2(signal[i].imag(), signal[i].real());
        float delta = unwrapPhase(phase, currentPhase);
        demod[i] = delta * FM_OUTPUT_SCALE;
        currentPhase = phase;
    }
    m_lastPhase = currentPhase;
    return demod;
}

std::vector<float> AudioDemodulator::decimate(const std::vector<float>& signal, int factor)
{
    if (factor <= 1) return signal;
    std::vector<float> decimated;
    decimated.reserve(signal.size() / factor + 1);
    for (size_t i = 0; i < signal.size(); i += factor) decimated.push_back(signal[i]);
    return decimated;
}

std::vector<float> AudioDemodulator::resample(const std::vector<float>& signal, double inputRate, double outputRate)
{
    if (signal.empty() || inputRate <= 0 || outputRate <= 0) return signal;
    double ratio = inputRate / outputRate;
    size_t outputSize = static_cast<size_t>(signal.size() / ratio);
    std::vector<float> resampled;
    resampled.reserve(outputSize);
    for (size_t i = 0; i < outputSize; i++) {
        double srcPos = i * ratio;
        size_t idx = static_cast<size_t>(srcPos);
        if (idx + 1 < signal.size()) {
            double frac = srcPos - idx;
            resampled.push_back(signal[idx] * (1.0 - frac) + signal[idx + 1] * frac);
        } else if (idx < signal.size()) {
            resampled.push_back(signal[idx]);
        }
    }
    return resampled;
}

void AudioDemodulator::emitAudioBuffer(const std::vector<float>& audio)
{
    if (audio.empty()) return;
    for (float sample : audio) {
        float processed = std::clamp(sample * m_audioGain, -1.0f, 1.0f);
        m_audioBuffer.push_back(processed);
    }
    static constexpr int CHUNK_SIZE = 480;
    while (m_audioBuffer.size() >= CHUNK_SIZE) {
        if (m_audioCallback)
            m_audioCallback(m_audioBuffer.data(), CHUNK_SIZE);
        m_audioBuffer.erase(m_audioBuffer.begin(), m_audioBuffer.begin() + CHUNK_SIZE);
    }
}

void AudioDemodulator::processSamples(const int8_t* data, size_t len)
{
    if (!data || len == 0 || !m_audioEnabled) return;
    std::lock_guard<std::mutex> lock(m_processMutex);
    std::vector<std::complex<float>> samples;
    samples.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2)
        samples.emplace_back(static_cast<float>(data[i]) / 128.0f,
                             static_cast<float>(data[i + 1]) / 128.0f);
    processSamples(samples);
}

void AudioDemodulator::processSamples(const std::vector<std::complex<float>>& samples)
{
    if (!m_audioEnabled || samples.empty() || !m_audioCapable) return;
    auto iq = frequencyShift(samples, -m_currentCarrierFreq);
    double currentRate = m_inputSampleRate;
    size_t stageIdx = 0;
    while (stageIdx < m_decimChain.size() && currentRate > 200000.0) {
        const auto& stage = m_decimChain[stageIdx];
        std::vector<float> realPart(iq.size()), imagPart(iq.size());
        for (size_t i = 0; i < iq.size(); i++) { realPart[i] = iq[i].real(); imagPart[i] = iq[i].imag(); }
        realPart = applyFIRFilter(realPart, stage.filterTaps);
        imagPart = applyFIRFilter(imagPart, stage.filterTaps);
        std::vector<std::complex<float>> decimated;
        decimated.reserve(iq.size() / stage.decimFactor + 1);
        for (size_t i = 0; i < realPart.size(); i += stage.decimFactor)
            decimated.emplace_back(realPart[i], imagPart[i]);
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
    if (std::abs(currentRate - 48000.0) > 1.0) audio = resample(audio, currentRate, 48000.0);
    audio = applyFIRFilter(audio, m_audioFilterTaps);
    emitAudioBuffer(audio);
}

void AudioDemodulator::setSampleRate(double newSampleRate)
{
    std::lock_guard<std::mutex> lock(m_processMutex);
    if (std::abs(newSampleRate - m_inputSampleRate) < 1.0) return;
    m_inputSampleRate = newSampleRate;
    m_audioPhaseIncrement = -2.0 * M_PI * m_currentCarrierFreq / m_inputSampleRate;
    m_audioPhase = 0.0;
    m_lastPhase = 0.0f;
    m_audioBuffer.clear();
    rebuildDecimationChain();
}
