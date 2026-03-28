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

// In-place FIR filter
void AudioDemodulator::applyFIRFilterInPlace(std::vector<float>& signal, const std::vector<float>& coeffs)
{
    if (signal.empty() || coeffs.empty()) return;
    const size_t signalSize = signal.size();
    const size_t filterSize = coeffs.size();
    const int halfTaps = filterSize / 2;
    const float* cData = coeffs.data();
    const float* sData = signal.data();

    m_firTemp.resize(signalSize);
    for (size_t i = 0; i < signalSize; i++) {
        float sum = 0.0f;
        const int iStart = std::max(0, (int)i - halfTaps);
        const int iEnd = std::min((int)signalSize, (int)i - halfTaps + (int)filterSize);
        const int jStart = iStart - ((int)i - halfTaps);
        for (int k = iStart, j = jStart; k < iEnd; k++, j++) {
            sum += sData[k] * cData[j];
        }
        m_firTemp[i] = sum;
    }
    signal.swap(m_firTemp);
}

// Combined filter + decimate for complex IQ: only compute every decimFactor'th output
// This is the KEY optimization: instead of filtering ALL samples then picking every Nth,
// we only compute the FIR output at positions 0, D, 2D, 3D... saving D× work.
static void filterDecimateComplex(
    const float* inReal, const float* inImag, size_t inSize,
    const std::vector<float>& taps, int decimFactor,
    std::vector<float>& outReal, std::vector<float>& outImag)
{
    const int numTaps = (int)taps.size();
    const int halfTaps = numTaps / 2;
    const float* cData = taps.data();
    size_t outCount = 0;

    // Pre-size outputs
    size_t maxOut = inSize / decimFactor + 1;
    outReal.resize(maxOut);
    outImag.resize(maxOut);

    for (size_t i = 0; i < inSize; i += decimFactor) {
        float sumR = 0.0f, sumI = 0.0f;
        const int iStart = std::max(0, (int)i - halfTaps);
        const int iEnd = std::min((int)inSize, (int)i - halfTaps + numTaps);
        const int jStart = iStart - ((int)i - halfTaps);
        for (int k = iStart, j = jStart; k < iEnd; k++, j++) {
            sumR += inReal[k] * cData[j];
            sumI += inImag[k] * cData[j];
        }
        outReal[outCount] = sumR;
        outImag[outCount] = sumI;
        outCount++;
    }
    outReal.resize(outCount);
    outImag.resize(outCount);
}

// Combined filter + decimate for real signal
static void filterDecimateReal(
    const std::vector<float>& signal,
    const std::vector<float>& taps, int decimFactor,
    std::vector<float>& out)
{
    const int numTaps = (int)taps.size();
    const int halfTaps = numTaps / 2;
    const float* cData = taps.data();
    const float* sData = signal.data();
    const size_t inSize = signal.size();
    size_t outCount = 0;

    out.resize(inSize / decimFactor + 1);
    for (size_t i = 0; i < inSize; i += decimFactor) {
        float sum = 0.0f;
        const int iStart = std::max(0, (int)i - halfTaps);
        const int iEnd = std::min((int)inSize, (int)i - halfTaps + numTaps);
        const int jStart = iStart - ((int)i - halfTaps);
        for (int k = iStart, j = jStart; k < iEnd; k++, j++) {
            sum += sData[k] * cData[j];
        }
        out[outCount++] = sum;
    }
    out.resize(outCount);
}

std::vector<std::complex<float>> AudioDemodulator::frequencyShift(
    const std::vector<std::complex<float>>& signal, double shiftFreq)
{
    if (std::abs(shiftFreq) < 1.0) return signal;
    std::vector<std::complex<float>> shifted(signal.size());
    double phaseInc = 2.0 * M_PI * shiftFreq / m_inputSampleRate;
    double phase = m_audioPhase;
    for (size_t i = 0; i < signal.size(); i++) {
        float cs = std::cos(phase), sn = std::sin(phase);
        shifted[i] = std::complex<float>(
            signal[i].real() * cs - signal[i].imag() * sn,
            signal[i].real() * sn + signal[i].imag() * cs);
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

// Fast FM demod: conjugate multiply
void AudioDemodulator::fmDemodulateInPlace(
    const std::vector<std::complex<float>>& signal, std::vector<float>& out, double signalRate)
{
    if (signal.empty()) { out.clear(); return; }
    out.resize(signal.size());
    std::lock_guard<std::mutex> lock(m_phaseMutex);
    float outputScale = m_radioMode ? 0.8f : 0.3f;

    // First sample
    {
        float phase = std::atan2(signal[0].imag(), signal[0].real());
        out[0] = unwrapPhase(phase, m_lastPhase) * outputScale;
    }
    // Conjugate multiply for remaining
    for (size_t i = 1; i < signal.size(); i++) {
        float pR = signal[i-1].real(), pI = signal[i-1].imag();
        float cR = signal[i].real(), cI = signal[i].imag();
        float crossImag = pR * cI - pI * cR;
        float crossReal = pR * cR + pI * cI;
        out[i] = std::atan2(crossImag, crossReal) * outputScale;
    }
    m_lastPhase = std::atan2(signal.back().imag(), signal.back().real());
}

void AudioDemodulator::decimateInPlace(std::vector<float>& signal, int factor)
{
    if (factor <= 1) return;
    size_t outSize = 0;
    for (size_t i = 0; i < signal.size(); i += factor)
        signal[outSize++] = signal[i];
    signal.resize(outSize);
}

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
            m_firTemp[i] = signal[idx] * (1.0f - (float)frac) + signal[idx + 1] * (float)frac;
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
        m_audioBuffer.push_back(std::clamp(sample * m_audioGain, -1.0f, 1.0f));
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

// Process raw int8 IQ: avoid intermediate complex vector allocation
void AudioDemodulator::processSamples(const int8_t* data, size_t len)
{
    if (!data || len == 0 || !m_audioEnabled) return;
    std::lock_guard<std::mutex> lock(m_processMutex);
    if (!m_audioCapable) return;

    const size_t numSamples = len / 2;

    // Split I/Q directly into work buffers (no complex<float> vector alloc)
    m_workReal.resize(numSamples);
    m_workImag.resize(numSamples);

    if (m_radioMode) {
        // Radio: no freq shift, direct copy
        for (size_t i = 0; i < numSamples; i++) {
            m_workReal[i] = data[i * 2] * (1.0f / 128.0f);
            m_workImag[i] = data[i * 2 + 1] * (1.0f / 128.0f);
        }
    } else {
        // TV: freq shift while converting
        double phaseInc = -2.0 * M_PI * m_currentCarrierFreq / m_inputSampleRate;
        double phase = m_audioPhase;
        for (size_t i = 0; i < numSamples; i++) {
            float sI = data[i * 2] * (1.0f / 128.0f);
            float sQ = data[i * 2 + 1] * (1.0f / 128.0f);
            float cs = std::cos(phase), sn = std::sin(phase);
            m_workReal[i] = sI * cs - sQ * sn;
            m_workImag[i] = sI * sn + sQ * cs;
            phase += phaseInc;
            if (phase > M_PI) phase -= 2.0 * M_PI;
            if (phase < -M_PI) phase += 2.0 * M_PI;
        }
        m_audioPhase = phase;
    }

    double currentRate = m_inputSampleRate;
    size_t stageIdx = 0;

    // Combined filter+decimate for complex IQ stages (D× less FIR work!)
    while (stageIdx < m_decimChain.size() && currentRate > 200000.0) {
        const auto& stage = m_decimChain[stageIdx];
        std::vector<float> outR, outI;
        filterDecimateComplex(
            m_workReal.data(), m_workImag.data(), m_workReal.size(),
            stage.filterTaps, stage.decimFactor, outR, outI);
        m_workReal.swap(outR);
        m_workImag.swap(outI);
        currentRate = stage.outputRate;
        stageIdx++;
    }

    // FM demod from I/Q floats (no complex<float> needed)
    {
        const size_t n = m_workReal.size();
        m_workAudio.resize(n);
        std::lock_guard<std::mutex> lock2(m_phaseMutex);
        float outputScale = m_radioMode ? 0.8f : 0.3f;
        // First sample
        if (n > 0) {
            float phase = std::atan2(m_workImag[0], m_workReal[0]);
            m_workAudio[0] = unwrapPhase(phase, m_lastPhase) * outputScale;
        }
        // Conjugate multiply
        for (size_t i = 1; i < n; i++) {
            float pR = m_workReal[i-1], pI = m_workImag[i-1];
            float cR = m_workReal[i], cI = m_workImag[i];
            m_workAudio[i] = std::atan2(pR*cI - pI*cR, pR*cR + pI*cI) * outputScale;
        }
        if (n > 0) m_lastPhase = std::atan2(m_workImag[n-1], m_workReal[n-1]);
    }

    // Remaining real-valued decimation stages: combined filter+decimate
    while (stageIdx < m_decimChain.size()) {
        const auto& stage = m_decimChain[stageIdx];
        std::vector<float> out;
        filterDecimateReal(m_workAudio, stage.filterTaps, stage.decimFactor, out);
        m_workAudio.swap(out);
        currentRate = stage.outputRate;
        stageIdx++;
    }

    if (std::abs(currentRate - 48000.0) > 1.0) resampleInPlace(m_workAudio, currentRate, 48000.0);
    applyFIRFilterInPlace(m_workAudio, m_audioFilterTaps);
    emitAudioBuffer(m_workAudio);
}

// Keep complex version for compatibility but route through optimized path
void AudioDemodulator::processSamples(const std::vector<std::complex<float>>& samples)
{
    if (!m_audioEnabled || samples.empty() || !m_audioCapable) return;
    // Convert to int8 and use optimized path (rare - only called from complex overload)
    std::vector<int8_t> buf(samples.size() * 2);
    for (size_t i = 0; i < samples.size(); i++) {
        buf[i*2] = (int8_t)std::clamp(samples[i].real() * 128.0f, -127.0f, 127.0f);
        buf[i*2+1] = (int8_t)std::clamp(samples[i].imag() * 128.0f, -127.0f, 127.0f);
    }
    processSamples(buf.data(), buf.size());
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
