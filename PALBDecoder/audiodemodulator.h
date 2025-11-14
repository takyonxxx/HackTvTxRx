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
    // PAL-B/G TV Standard Constants
    static constexpr int SAMP_RATE = 16000000;           // 16 MHz (HackRF sample rate)
    static constexpr int AUDIO_SAMP_RATE = 48000;        // 48 kHz output
    static constexpr int AUDIO_BUFFER_SIZE = 480;        // 10ms @ 48kHz (match AudioOutput)
    
    // PAL-B Audio Specifications
    static constexpr double AUDIO_CARRIER = 5.5e6;       // 5.5 MHz (PAL-B standard)
    static constexpr double FM_DEVIATION = 50e3;         // ±50 kHz (PAL standard)
    
    // Optimized decimation: 16MHz →÷5→ 3.2MHz →÷10→ 320kHz →÷2→ 160kHz →÷3.33→ 48kHz
    static constexpr int FILTER_TAPS = 17;

    // Thread safety
    QMutex m_processMutex;
    mutable QRecursiveMutex m_mutex;
    QMutex m_phaseMutex;

    // Audio filters (FIR) - CACHED
    std::vector<float> m_audioFilterTaps;
    std::vector<float> m_decimFilter1;  // 1.28 MHz @ 16 MHz
    std::vector<float> m_decimFilter2;  // 128 kHz @ 3.2 MHz
    std::vector<float> m_decimFilter3;  // 64 kHz @ 320 kHz
    std::vector<float> m_decimFilter4;  // 21.3 kHz @ 160 kHz

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
    
    // Resampling (for non-integer decimation)
    std::vector<float> resample(const std::vector<float>& signal,
                                double inputRate,
                                double outputRate);

    // Output processing
    void emitAudioBuffer(const std::vector<float>& audio);
};

#endif // AUDIODEMODULATOR_H
