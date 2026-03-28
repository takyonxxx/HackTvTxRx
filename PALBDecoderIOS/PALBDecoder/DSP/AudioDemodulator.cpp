#include "AudioDemodulator.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AudioDemodulator::AudioDemodulator()
    : m_inputSampleRate(16000000.0)
    , m_audioCapable(true)
    , m_radioMode(false)
    , m_lastPhase(0.0f)
    , m_audioPhase(0.0)
    , m_audioGain(1.0f)
    , m_audioEnabled(true)
    , fmDeviation(FM_DEVIATION_TV)
{
    m_audioBuffer.reserve(AUDIO_BUFFER_SIZE * 2);
    m_workReal.reserve(262144);
    m_workImag.reserve(262144);
    m_workAudio.reserve(262144);
    m_firTemp.reserve(262144);
    m_currentCarrierFreq = AUDIO_CARRIER;
    m_audioPhaseIncrement = -2.0 * M_PI * AUDIO_CARRIER / m_inputSampleRate;
    initFinalFilter();
    rebuildDecimationChain();
}

void AudioDemodulator::setRadioMode(bool radio)
{
    std::lock_guard<std::mutex> lock(m_processMutex);
    m_radioMode = radio;
    if (radio) {
        m_currentCarrierFreq = 0.0;
        fmDeviation = FM_DEVIATION_RADIO;
        m_audioCapable = true;
    } else {
        fmDeviation = FM_DEVIATION_TV;
    }
    m_audioPhase = 0.0;
    m_lastPhase = 0.0f;
    m_audioBuffer.clear();
    initFinalFilter();
    rebuildDecimationChain();
}

void AudioDemodulator::setAudioCarrierFreq(double freqHz)
{
    if (m_radioMode) return;
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

    if (!m_radioMode && m_currentCarrierFreq >= nyquist) {
        m_audioCapable = false;
        return;
    }
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

// In-place FIR filter - uses pre-allocated m_firTemp, no malloc
void AudioDemodulator::applyFIRFilterInPlace(std::vector<float>& signal, const std::vector<float>& coeffs)
{
    if (signal.empty() || coeffs.empty()) return;
    const size_t signalSize = signal.size();
    const size_t filterSize = coeffs.size();
    const int halfTaps = filterSize / 2;

    m_firTemp.resize(signalSize);
    for (size_t i = 0; i < signalSize; i++) {
        float sum = 0.0f;
        const int iStart = std::max(0, (int)i - halfTaps);
        const int iEnd = std::min((int)signalSize, (int)i - halfTaps + (int)filterSize);
        const int jStart = iStart - ((int)i - halfTaps);
        for (int k = iStart, j = jStart; k < iEnd; k++, j++) {
            sum += signal[k] * coeffs[j];
        }
        m_firTemp[i] = sum;
    }
    signal.swap(m_firTemp);
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

// Fast FM demod using conjugate multiply - avoids expensive atan2
void AudioDemodulator::fmDemodulateInPlace(
    const std::vector<std::complex<float>>& signal, std::vector<float>& out, double signalRate)
{
    if (signal.empty()) { out.clear(); return; }
    out.resize(signal.size());
    std::lock_guard<std::mutex> lock(m_phaseMutex);
    float outputScale = m_radioMode ? 0.8f : 0.3f;

    // First sample uses stored last phase
    {
        float phase = std::atan2(signal[0].imag(), signal[0].real());
        float delta = unwrapPhase(phase, m_lastPhase);
        out[0] = delta * outputScale;
    }

    // Remaining samples: conjugate multiply (prev* × curr).imag ≈ phase delta
    // Much faster than atan2 per sample
    for (size_t i = 1; i < signal.size(); i++) {
        // conj(prev) * curr = (pR + j*(-pI)) * (cR + j*cI) = (pR*cR + pI*cI) + j*(pR*cI - pI*cR)
        float pR = signal[i-1].real(), pI = signal[i-1].imag();
        float cR = signal[i].real(), cI = signal[i].imag();
        float crossImag = pR * cI - pI * cR;
        float crossReal = pR * cR + pI * cI;
        // Fast atan2 approximation for small angles: atan2(y,x) ≈ y/x when |y/x| < 1
        // For FM with reasonable deviation this is accurate enough
        float mag2 = crossReal * crossReal + crossImag * crossImag;
        float delta;
        if (mag2 > 1e-12f) {
            delta = std::atan2(crossImag, crossReal);
        } else {
            delta = 0.0f;
        }
        out[i] = delta * outputScale;
    }

    // Store last phase for continuity
    m_lastPhase = std::atan2(signal.back().imag(), signal.back().real());
}

// In-place decimate
void AudioDemodulator::decimateInPlace(std::vector<float>& signal, int factor)
{
    if (factor <= 1) return;
    size_t outSize = 0;
    for (size_t i = 0; i < signal.size(); i += factor)
        signal[outSize++] = signal[i];
    signal.resize(outSize);
}

// In-place resample
void AudioDemodulator::resampleInPlace(std::vector<float>& signal, double inputRate, double outputRate)
{
    if (signal.empty() || inputRate <= 0 || outputRate <= 0) return;
    double ratio = inputRate / outputRate;
    size_t outputSize = static_cast<size_t>(signal.size() / ratio);

    m_firTemp.resize(outputSize);
    for (size_t i = 0; i < outputSize; i++) {
        double srcPos = i * ratio;
        size_t idx = static_cast<size_t>(srcPos);
        if (idx + 1 < signal.size()) {
            double frac = srcPos - idx;
            m_firTemp[i] = signal[idx] * (1.0 - frac) + signal[idx + 1] * frac;
        } else if (idx < signal.size()) {
            m_firTemp[i] = signal[idx];
        } else {
            m_firTemp[i] = 0;
        }
    }
    signal.swap(m_firTemp);
}

void AudioDemodulator::emitAudioBuffer(const std::vector<float>& audio)
{
    if (audio.empty()) return;
    for (float sample : audio) {
        float processed = std::clamp(sample * m_audioGain, -1.0f, 1.0f);
        m_audioBuffer.push_back(processed);
    }
    static constexpr int CHUNK_SIZE = 480;
    size_t emitted = 0;
    while (m_audioBuffer.size() - emitted >= CHUNK_SIZE) {
        if (m_audioCallback)
            m_audioCallback(m_audioBuffer.data() + emitted, CHUNK_SIZE);
        emitted += CHUNK_SIZE;
    }
    if (emitted > 0) {
        size_t remaining = m_audioBuffer.size() - emitted;
        if (remaining > 0)
            memmove(m_audioBuffer.data(), m_audioBuffer.data() + emitted, remaining * sizeof(float));
        m_audioBuffer.resize(remaining);
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

    // Radio: IQ already at baseband, no shift needed - use directly
    // TV: shift audio carrier to baseband
    std::vector<std::complex<float>> shiftedIQ;
    const std::vector<std::complex<float>>& iqRef = m_radioMode
        ? samples
        : (shiftedIQ = frequencyShift(samples, -m_currentCarrierFreq), shiftedIQ);

    double currentRate = m_inputSampleRate;
    size_t stageIdx = 0;

    // For decimation we need a mutable copy (only if decimating)
    std::vector<std::complex<float>> iq;
    bool needDecim = !m_decimChain.empty() && currentRate > 200000.0;
    if (needDecim) {
        iq = iqRef;  // copy only when needed
    }
    const std::vector<std::complex<float>>& iqWork = needDecim ? iq : iqRef;

    // Decimate complex IQ until rate <= 200 kHz
    while (stageIdx < m_decimChain.size() && currentRate > 200000.0) {
        const auto& stage = m_decimChain[stageIdx];
        m_workReal.resize(iq.size());
        m_workImag.resize(iq.size());
        for (size_t i = 0; i < iq.size(); i++) { m_workReal[i] = iq[i].real(); m_workImag[i] = iq[i].imag(); }
        applyFIRFilterInPlace(m_workReal, stage.filterTaps);
        applyFIRFilterInPlace(m_workImag, stage.filterTaps);
        iq.clear();
        iq.reserve(m_workReal.size() / stage.decimFactor + 1);
        for (size_t i = 0; i < m_workReal.size(); i += stage.decimFactor)
            iq.emplace_back(m_workReal[i], m_workImag[i]);
        currentRate = stage.outputRate;
        stageIdx++;
    }

    fmDemodulateInPlace(iqWork, m_workAudio, currentRate);

    // Continue remaining decimation (real-valued)
    while (stageIdx < m_decimChain.size()) {
        const auto& stage = m_decimChain[stageIdx];
        applyFIRFilterInPlace(m_workAudio, stage.filterTaps);
        decimateInPlace(m_workAudio, stage.decimFactor);
        currentRate = stage.outputRate;
        stageIdx++;
    }

    if (std::abs(currentRate - 48000.0) > 1.0) resampleInPlace(m_workAudio, currentRate, 48000.0);
    applyFIRFilterInPlace(m_workAudio, m_audioFilterTaps);
    emitAudioBuffer(m_workAudio);
}

void AudioDemodulator::setSampleRate(double newSampleRate)
{
    std::lock_guard<std::mutex> lock(m_processMutex);
    if (std::abs(newSampleRate - m_inputSampleRate) < 1.0) return;
    m_inputSampleRate = newSampleRate;
    if (!m_radioMode)
        m_audioPhaseIncrement = -2.0 * M_PI * m_currentCarrierFreq / m_inputSampleRate;
    m_audioPhase = 0.0;
    m_lastPhase = 0.0f;
    m_audioBuffer.clear();
    rebuildDecimationChain();
}
