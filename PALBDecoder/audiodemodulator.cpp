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
    , m_lastPhase(0.0f)
    , m_audioPhase(0.0)
    , m_audioGain(5.0f)
    , m_audioEnabled(true)
    , sampleRate(AUDIO_SAMP_RATE)
    , fmDeviation(FM_DEVIATION)
{
    m_audioBuffer.reserve(AUDIO_BUFFER_SIZE * 2);
    m_audioPhaseIncrement = -2.0 * M_PI * AUDIO_CARRIER / SAMP_RATE;

    initFilters();

    qDebug() << "========================================";
    qDebug() << "AudioDemodulator initialized (PAL-B):";
    qDebug() << "  Sample rate: 16 MHz → 48 kHz (÷333.33)";
    qDebug() << "  Decimation: 16MHz →÷5→ 3.2MHz →÷10→ 320kHz →÷2→ 160kHz →÷10/3→ 48kHz";
    qDebug() << "  Audio carrier: 5.5 MHz";
    qDebug() << "  FM deviation: ±50 kHz";
    qDebug() << "  Audio gain:" << m_audioGain;
    qDebug() << "========================================";
}

AudioDemodulator::~AudioDemodulator()
{
}

void AudioDemodulator::initFilters()
{
    // Audio bandwidth filter (15 kHz for PAL-B)
    m_audioFilterTaps = designLowPassFIR(FILTER_TAPS, 15000.0f, 48000.0f);
    
    // Pre-calculate decimation filter coefficients (PERFORMANCE OPTIMIZATION)
    m_decimFilter1 = designLowPassFIR(FILTER_TAPS, 1280000.0f, 16000000.0f);
    m_decimFilter2 = designLowPassFIR(FILTER_TAPS, 128000.0f, 3200000.0f);
    m_decimFilter3 = designLowPassFIR(FILTER_TAPS, 64000.0f, 320000.0f);
    m_decimFilter4 = designLowPassFIR(FILTER_TAPS, 21333.0f, 160000.0f);
    
    qDebug() << "Audio filters initialized:";
    qDebug() << "  Final: 15kHz @ 48kHz";
    qDebug() << "  Decim filters: 4 stages cached";
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
    double phaseInc = 2.0 * M_PI * shiftFreq / sampleRate;
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

    for (size_t i = 0; i < signal.size(); i++) {
        float phase = std::atan2(signal[i].imag(), signal[i].real());
        float delta = unwrapPhase(phase, currentPhase);

        demod[i] = delta * sampleRate / (2.0f * M_PI * fmDeviation);

        currentPhase = phase;
    }

    phaseLock.relock();
    m_lastPhase = currentPhase;

    return demod;
}

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

    static QElapsedTimer emitTimer;
    if (!emitTimer.isValid()) emitTimer.start();

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

    try {
        // 1. Frequency shift to baseband
        auto audioSignal = frequencyShift(samples, -AUDIO_CARRIER);

        // 2. FM demodulate
        auto audio = fmDemodulateAtan2(audioSignal);

        double currentRate = SAMP_RATE;

        // Stage 1: 16 MHz → 3.2 MHz (÷5)
        audio = applyFIRFilter(audio, m_decimFilter1);
        audio = decimate(audio, 5);
        currentRate = 3.2e6;

        // Stage 2: 3.2 MHz → 320 kHz (÷10)
        audio = applyFIRFilter(audio, m_decimFilter2);
        audio = decimate(audio, 10);
        currentRate = 320e3;

        // Stage 3: 320 kHz → 160 kHz (÷2)
        audio = applyFIRFilter(audio, m_decimFilter3);
        audio = decimate(audio, 2);
        currentRate = 160e3;

        // Stage 4: 160 kHz → 53.33 kHz (÷3)
        audio = applyFIRFilter(audio, m_decimFilter4);
        audio = decimate(audio, 3);
        currentRate = 160e3 / 3.0;

        // Final resample: 53.33 kHz → 48 kHz
        audio = resample(audio, currentRate, 48000.0);
        currentRate = 48000.0;

        // Final audio filter at 15 kHz
        audio = applyFIRFilter(audio, m_audioFilterTaps);
        // Emit
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

    // Optimized chain with cached filters
    audio = applyFIRFilter(audio, m_decimFilter1);
    audio = decimate(audio, 5);

    audio = applyFIRFilter(audio, m_decimFilter2);
    audio = decimate(audio, 10);

    audio = applyFIRFilter(audio, m_decimFilter3);
    audio = decimate(audio, 2);

    audio = applyFIRFilter(audio, m_decimFilter4);
    audio = decimate(audio, 3);

    audio = resample(audio, 160e3 / 3.0, 48000.0);
    audio = applyFIRFilter(audio, m_audioFilterTaps);

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
