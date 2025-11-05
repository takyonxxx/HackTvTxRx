#ifndef AUDIODEMODULATOR_H
#define AUDIODEMODULATOR_H

#include <QObject>
#include <vector>
#include <complex>
#include <deque>
#include <cstdint>
#include <cmath>

class AudioDemodulator : public QObject
{
    Q_OBJECT
public:
    explicit AudioDemodulator(QObject *parent = nullptr);
    ~AudioDemodulator();

    void processSamples(const int8_t* data, size_t len);
    void processSamples(const std::vector<std::complex<float>>& samples);

    // Controls
    void setAudioGain(float gain) { m_audioGain = gain; }
    void setAudioEnabled(bool enabled) { m_audioEnabled = enabled; }

    float getAudioGain() const { return m_audioGain; }
    bool getAudioEnabled() const { return m_audioEnabled; }

signals:
    void audioReady(const std::vector<float>& audioSamples);

private:
    // Constants
    static constexpr int SAMP_RATE = 16000000;
    static constexpr int AUDIO_SAMP_RATE = 48000;
    static constexpr int AUDIO_BUFFER_SIZE = 1920;  // 40ms at 48kHz
    static constexpr float AUDIO_DEVIATION = 50000.0f;  // 50 kHz FM deviation

    // Audio decimation: 16 MHz → 48 kHz = 333.33
    static constexpr int AUDIO_RESAMPLE_DECIM = 333;

    // Audio filters
    std::vector<float> m_audioFilterTaps;
    std::deque<float> m_audioFilterDelay;

    // Audio resampler
    int m_audioResampleCounter;
    std::vector<float> m_audioBuffer;

    // FM demodulator state
    std::complex<float> m_lastAudioSample;

    // Settings
    float m_audioGain;
    bool m_audioEnabled;

    // Stats
    uint64_t m_totalSamples;
    uint64_t m_audioBuffersEmitted;

    // Helper functions
    void initFilters();
    std::vector<float> designLowPassFIR(float cutoff, float sampleRate, int numTaps);
    float applyAudioFilter(float sample);
    float fmDemodulate(const std::complex<float>& sample);
    void processAudioSample(float sample);
    float clipValue(float value, float min, float max);
};

#endif // AUDIODEMODULATOR_H
