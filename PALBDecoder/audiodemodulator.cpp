#include "AudioDemodulator.h"
#include <QElapsedTimer>
#include <QDebug>
#include <algorithm>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AudioDemodulator::AudioDemodulator(QObject *parent)
    : QObject(parent)
    , m_lastPhase(0.0f)
    , m_audioPhase(0.0)
    , m_audioGain(1.0f)
    , m_audioEnabled(true)
    , sampleRate(SAMP_RATE)
    , fmDeviation(FM_DEVIATION)
{
    m_audioBuffer.reserve(AUDIO_BUFFER_SIZE + 100);

    // Calculate phase increment for audio carrier frequency shift
    m_audioPhaseIncrement = -2.0 * M_PI * AUDIO_CARRIER / SAMP_RATE;

    initFilters();

    qDebug() << "AudioDemodulator initialized:";
    qDebug() << "  Sample rate:" << SAMP_RATE << "Hz → 48 kHz";
    qDebug() << "  Decimation:" << AUDIO_DECIM;
    qDebug() << "  Buffer size:" << AUDIO_BUFFER_SIZE << "samples (100ms)";
    qDebug() << "  FM method: atan2 + unwrapPhase";
    qDebug() << "  Audio carrier:" << AUDIO_CARRIER / 1e6 << "MHz";
    qDebug() << "  FM deviation:" << FM_DEVIATION / 1e3 << "kHz";
    qDebug() << "  Filter: 65-tap FIR, 15 kHz cutoff";
}

AudioDemodulator::~AudioDemodulator()
{

}

void AudioDemodulator::initFilters()
{
    // 15 kHz lowpass filter
    m_audioFilterTaps = designLowPassFIR(FILTER_TAPS, 15000.0f, SAMP_RATE);
    qDebug() << "Audio filter initialized:" << m_audioFilterTaps.size() << "taps";
}

// ============================================================================
// FIR FILTER DESIGN
// ============================================================================
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

        // Hamming window
        float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (numTaps - 1));
        coeffs[i] *= window;
    }

    // Normalize
    float sum = std::accumulate(coeffs.begin(), coeffs.end(), 0.0f);
    if (sum != 0) {
        for (auto& c : coeffs) c /= sum;
    }

    return coeffs;
}

// ============================================================================
// FIR FILTER APPLICATION
// ============================================================================

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

// ============================================================================
// FREQUENCY SHIFT
// ============================================================================

std::vector<std::complex<float>> AudioDemodulator::frequencyShift(
    const std::vector<std::complex<float>>& signal,
    double shiftFreq)
{
    if (std::abs(shiftFreq) < 1.0) return signal;

    std::vector<std::complex<float>> shifted(signal.size());
    double phaseInc = 2.0 * M_PI * shiftFreq / sampleRate;
    double phase = 0.0;

    for (size_t i = 0; i < signal.size(); i++) {
        std::complex<float> shift(std::cos(phase), std::sin(phase));
        shifted[i] = signal[i] * shift;
        phase += phaseInc;

        if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
        if (phase < -2.0 * M_PI) phase += 2.0 * M_PI;
    }

    return shifted;
}

// ============================================================================
// PHASE UNWRAP
// ============================================================================
float AudioDemodulator::unwrapPhase(float phase, float lastPhase)
{
    float delta = phase - lastPhase;

    while (delta > M_PI) delta -= 2.0f * M_PI;
    while (delta < -M_PI) delta += 2.0f * M_PI;

    return delta;
}

// ============================================================================
// FM DEMODULATION - ATAN2 METHOD
// ============================================================================

std::vector<float> AudioDemodulator::fmDemodulateAtan2(
    const std::vector<std::complex<float>>& signal)
{
    if (signal.empty()) return std::vector<float>();

    std::vector<float> demod(signal.size());

    // Thread-safe phase access
    QMutexLocker phaseLock(&m_phaseMutex);
    float currentPhase = m_lastPhase;
    phaseLock.unlock();

    for (size_t i = 0; i < signal.size(); i++) {
        float phase = std::atan2(signal[i].imag(), signal[i].real());
        float delta = unwrapPhase(phase, currentPhase);

        demod[i] = delta * sampleRate / (2.0f * M_PI * fmDeviation);

        currentPhase = phase;
    }

    // Save last phase for next batch
    phaseLock.relock();
    m_lastPhase = currentPhase;

    return demod;
}

// ============================================================================
// LOW-PASS FILTER
// ============================================================================

std::vector<float> AudioDemodulator::lowPassFilter(
    const std::vector<float>& signal,
    float cutoffFreq)
{
    if (signal.empty()) return signal;

    if (signal.size() > 10000000) {
        qCritical() << "Signal too large for filtering:" << signal.size();
        return std::vector<float>();
    }

    try {
        std::vector<float> coeffs = designLowPassFIR(65, cutoffFreq, sampleRate);
        return applyFIRFilter(signal, coeffs);
    }
    catch (const std::exception& e) {
        qCritical() << "Filter error:" << e.what();
        return std::vector<float>();
    }
}

// ============================================================================
// DECIMATION
// ============================================================================
std::vector<float> AudioDemodulator::decimate(
    const std::vector<float>& signal,
    int factor)
{
    if (factor <= 1) return signal;

    std::vector<float> decimated;
    decimated.reserve(signal.size() / factor);

    for (size_t i = 0; i < signal.size(); i += factor) {
        decimated.push_back(signal[i]);
    }

    return decimated;
}

// ============================================================================
// EMIT AUDIO BUFFER
// ============================================================================
void AudioDemodulator::emitAudioBuffer(const std::vector<float>& audio)
{
    if (audio.empty()) return;

    static constexpr int CHUNK_SIZE = 480; // 10ms @ 48kHz

    // Add to buffer
    for (float sample : audio) {
        float processed = sample * m_audioGain;
        processed = std::clamp(processed, -1.0f, 1.0f);
        m_audioBuffer.push_back(processed);
    }

    // Emit fixed-size chunks
    while (m_audioBuffer.size() >= CHUNK_SIZE) {
        std::vector<float> chunk(m_audioBuffer.begin(),
                                 m_audioBuffer.begin() + CHUNK_SIZE);
        emit audioReady(chunk);

        m_audioBuffer.erase(m_audioBuffer.begin(),
                            m_audioBuffer.begin() + CHUNK_SIZE);

        qDebug() << "✓ Emitted 480 samples (10ms), remaining:" << m_audioBuffer.size();
    }
}

// ============================================================================
// MAIN PROCESSING - THREAD-SAFE WITH MUTEX
// ============================================================================
void AudioDemodulator::processSamples(const int8_t* data, size_t len)
{
    if (!data || len == 0 || !m_audioEnabled) return;

    QMutexLocker locker(&m_processMutex);

    // Convert int8_t to complex<float>
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

    if (!m_audioEnabled || samples.empty()) return;

    try {
        static int callCount = 0;
        callCount++;

        qDebug() << "=== Audio Process #" << callCount << "===";
        qDebug() << "Input samples:" << samples.size();

        // 1. Frequency shift
        auto audioSignal = frequencyShift(samples, -AUDIO_CARRIER);

        // 2. FM demodulate
        auto audio = fmDemodulateAtan2(audioSignal);
        qDebug() << "After demod:" << audio.size();

        // 3. Two-stage decimation
        int stage1Factor = static_cast<int>(SAMP_RATE / 192000.0);
        audio = lowPassFilter(audio, 76800.0f);
        audio = decimate(audio, stage1Factor);
        qDebug() << "After stage1 (÷" << stage1Factor << "):" << audio.size();

        audio = lowPassFilter(audio, 19200.0f);
        audio = decimate(audio, 4);
        qDebug() << "After stage2 (÷4):" << audio.size();

        audio = lowPassFilter(audio, 15000.0f);
        qDebug() << "Final audio samples:" << audio.size();

        // 5. Emit
        emitAudioBuffer(audio);

    } catch (const std::exception& e) {
        qCritical() << "Audio processing error:" << e.what();
    }
}

std::vector<float> AudioDemodulator::demodulateAudio(
    const std::vector<std::complex<float>>& samples)
{
    QMutexLocker lock(&m_mutex);

    if (samples.empty() || AUDIO_CARRIER <= 0) {
        return std::vector<float>();
    }

    auto audioSignal = frequencyShift(samples, -AUDIO_CARRIER);
    auto audio = fmDemodulateAtan2(audioSignal);

    if (sampleRate > 48000) {
        // Multi-stage decimation
        double currentRate = sampleRate;
        const double targetRate = 48000.0;

        while (currentRate > targetRate * 1.5) {
            // Her adımda maksimum 16x decimate et
            int factor = std::min(16, static_cast<int>(currentRate / targetRate));
            if (factor < 2) break;

            // Anti-aliasing filter
            float cutoff = (currentRate / factor / 2.0f) * 0.8f;
            audio = lowPassFilter(audio, cutoff);
            audio = decimate(audio, factor);

            currentRate /= factor;
        }

        // Final low-pass at 15 kHz
        audio = lowPassFilter(audio, 15000.0f);
    } else {
        audio = lowPassFilter(audio, 15000.0f);
    }

    return audio;
}

double AudioDemodulator::getSampleRate() const
{
    return sampleRate;
}

void AudioDemodulator::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate;
}
