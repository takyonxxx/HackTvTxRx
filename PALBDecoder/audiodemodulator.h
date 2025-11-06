#ifndef AUDIODEMODULATOR_H
#define AUDIODEMODULATOR_H

#include <QObject>
#include <QMutex>
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

    // Processing functions
    void processSamples(const int8_t* data, size_t len);
    void processSamples(const std::vector<std::complex<float>>& samples);
    std::vector<float> demodulateAudio(const std::vector<std::complex<float>>& samples);

    // Controls
    void setAudioGain(float gain) { m_audioGain = gain; }
    void setAudioEnabled(bool enabled) { m_audioEnabled = enabled; }

    float getAudioGain() const { return m_audioGain; }
    bool getAudioEnabled() const { return m_audioEnabled; }

    double getSampleRate() const;

    void setSampleRate(double newSampleRate);

signals:
    void audioReady(const std::vector<float>& audioSamples);

private:
    // Constants (PAL-B specific)
    static constexpr int SAMP_RATE = 16000000;           // 16 MHz
    static constexpr int AUDIO_SAMP_RATE = 48000;        // 48 kHz output
    static constexpr int AUDIO_BUFFER_SIZE = 480;        // 20ms @ 48kHz
    static constexpr double AUDIO_CARRIER = 5.5e6;       // 5.5 MHz
    static constexpr double FM_DEVIATION = 6.0e6;        // 6 MHz
    // Audio decimation: 16 MHz → 48 kHz ≈ 333
    static constexpr int AUDIO_DECIM = 333;
    static constexpr int FILTER_TAPS = 65;

    // Thread safety

    // Audio filters (FIR)
    std::vector<float> m_audioFilterTaps;

    // Audio buffer
    std::vector<float> m_audioBuffer;

    // FM demodulator state (atan2 method)
    float m_lastPhase;

    // Frequency shift state
    double m_audioPhase;
    double m_audioPhaseIncrement;

    // Settings
    float m_audioGain;
    bool m_audioEnabled;

    QMutex m_processMutex;
    mutable QRecursiveMutex m_mutex;
    QMutex m_phaseMutex;

    double sampleRate;
    double fmDeviation;

    // Helper functions
    void initFilters();
    std::vector<float> designLowPassFIR(int numTaps, float cutoffFreq, float sampleRate);
    std::vector<float> applyFIRFilter(const std::vector<float>& signal,
                                      const std::vector<float>& coeffs);

    // Frequency shift
    std::vector<std::complex<float>> frequencyShift(
        const std::vector<std::complex<float>>& signal,
        double shiftFreq);

    // FM demodulation (atan2 method)
    std::vector<float> fmDemodulateAtan2(const std::vector<std::complex<float>>& signal);

    // Phase unwrap
    float unwrapPhase(float phase, float lastPhase);

    // Low-pass filter
    std::vector<float> lowPassFilter(const std::vector<float>& signal, float cutoffFreq);

    // Decimation
    std::vector<float> decimate(const std::vector<float>& signal, int factor);

    // Output processing
    void emitAudioBuffer(const std::vector<float>& audio);
};

#endif // AUDIODEMODULATOR_H
