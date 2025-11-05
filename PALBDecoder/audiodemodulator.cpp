#include "AudioDemodulator.h"
#include <QDebug>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AudioDemodulator::AudioDemodulator(QObject *parent)
    : QObject(parent)
    , m_audioResampleCounter(0)
    , m_lastAudioSample(0.0f, 0.0f)
    , m_audioGain(1.0f)
    , m_audioEnabled(true)
    , m_totalSamples(0)
    , m_audioBuffersEmitted(0)
{
    m_audioBuffer.reserve(AUDIO_BUFFER_SIZE + 100);
    initFilters();

    qDebug() << "AudioDemodulator initialized:";
    qDebug() << "  Sample rate: 16 MHz → 48 kHz";
    qDebug() << "  Decimation:" << AUDIO_RESAMPLE_DECIM;
    qDebug() << "  Buffer size:" << AUDIO_BUFFER_SIZE << "samples (40ms)";
    qDebug() << "  FM deviation:" << AUDIO_DEVIATION << "Hz";
}

AudioDemodulator::~AudioDemodulator()
{
    qDebug() << "AudioDemodulator: Emitted" << m_audioBuffersEmitted << "audio buffers";
}

void AudioDemodulator::initFilters()
{
    // 15 kHz lowpass filter for audio
    m_audioFilterTaps = designLowPassFIR(15000.0f, SAMP_RATE, 65);
}

std::vector<float> AudioDemodulator::designLowPassFIR(float cutoff, float sampleRate, int numTaps)
{
    std::vector<float> taps(numTaps);
    float fc = cutoff / sampleRate;
    int M = numTaps - 1;

    for (int n = 0; n < numTaps; n++) {
        float mm = n - M / 2.0f;
        float h = (mm == 0.0f) ? 2.0f * fc : std::sin(2.0f * M_PI * fc * mm) / (M_PI * mm);
        float w = 0.54f - 0.46f * std::cos(2.0f * M_PI * n / M);  // Hamming window
        taps[n] = h * w;
    }

    float sum = 0.0f;
    for (float tap : taps) sum += tap;
    if (sum != 0.0f) {
        for (float& tap : taps) tap /= sum;
    }

    return taps;
}

float AudioDemodulator::applyAudioFilter(float sample)
{
    m_audioFilterDelay.push_front(sample);
    if (m_audioFilterDelay.size() > m_audioFilterTaps.size()) {
        m_audioFilterDelay.pop_back();
    }

    float output = 0.0f;
    size_t n = std::min(m_audioFilterDelay.size(), m_audioFilterTaps.size());
    for (size_t i = 0; i < n; i++) {
        output += m_audioFilterDelay[i] * m_audioFilterTaps[i];
    }
    return output;
}

float AudioDemodulator::fmDemodulate(const std::complex<float>& sample)
{
    // FM demodulation: phase difference between consecutive samples
    std::complex<float> product = sample * std::conj(m_lastAudioSample);
    m_lastAudioSample = sample;

    // Extract phase difference
    float phase = std::atan2(product.imag(), product.real());

    // Scale to audio range
    float gain = SAMP_RATE / (2.0f * M_PI * AUDIO_DEVIATION);
    return phase * gain;
}

void AudioDemodulator::processAudioSample(float sample)
{
    // Apply gain
    sample *= m_audioGain;

    // Clip to valid range
    sample = clipValue(sample, -1.0f, 1.0f);

    m_audioBuffer.push_back(sample);

    // Emit when buffer is full
    if (m_audioBuffer.size() >= AUDIO_BUFFER_SIZE) {
        m_audioBuffersEmitted++;

        // Debug first few
        if (m_audioBuffersEmitted <= 3) {
            qDebug() << "AudioDemodulator: Emitting buffer" << m_audioBuffersEmitted
                     << "with" << m_audioBuffer.size() << "samples";
        }

        emit audioReady(m_audioBuffer);
        m_audioBuffer.clear();
    }
}

float AudioDemodulator::clipValue(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

void AudioDemodulator::processSamples(const int8_t* data, size_t len)
{
    if (!data || len == 0 || !m_audioEnabled) return;

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
    if (!m_audioEnabled) return;

    for (const auto& sample : samples) {
        m_totalSamples++;

        // Stats every 100M samples
        if (m_totalSamples % 100000000 == 0) {
            qDebug() << "AudioDemodulator: Processed" << (m_totalSamples / 1000000)
            << "M samples, emitted" << m_audioBuffersEmitted << "buffers";
        }

        // FM demodulate
        float audioFM = fmDemodulate(sample);

        // Audio filter (15 kHz LPF)
        float audioFiltered = applyAudioFilter(audioFM);

        // Audio decimation to 48 kHz
        m_audioResampleCounter++;
        if (m_audioResampleCounter >= AUDIO_RESAMPLE_DECIM) {
            m_audioResampleCounter = 0;
            processAudioSample(audioFiltered);
        }
    }
}
