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

    // Returns true if audio can be decoded at the current sample rate
    // (i.e. audio carrier < Nyquist)
    bool isAudioCapable() const { return m_audioCapable; }

    // Set the actual audio carrier frequency in baseband (accounts for tune offset)
    // audioCarrierHz = videoCarrierOffsetHz + 5.5e6
    void setAudioCarrierFreq(double freqHz);

signals:
    void audioReady(const std::vector<float>& audioSamples);
    void audioCapabilityChanged(bool capable, double sampleRate, double carrierFreq);

private:
    // PAL-B/G TV Standard Constants
    static constexpr int AUDIO_SAMP_RATE = 48000;        // 48 kHz output
    static constexpr int AUDIO_BUFFER_SIZE = 480;        // 10ms @ 48kHz (match AudioOutput)

    // PAL-B Audio Specifications
    static constexpr double AUDIO_CARRIER = 5.5e6;       // 5.5 MHz (PAL-B standard)
    static constexpr double FM_DEVIATION = 50e3;          // +/-50 kHz (PAL standard)

    static constexpr int FILTER_TAPS = 17;

    // Thread safety
    QMutex m_processMutex;
    mutable QRecursiveMutex m_mutex;
    QMutex m_phaseMutex;

    // ========== Dynamic decimation chain ==========
    // Computed by rebuildDecimationChain() when sample rate changes.
    // Each stage: filter + integer decimate (or final resample to 48 kHz).
    struct DecimStage {
        std::vector<float> filterTaps;
        int decimFactor;       // integer decimation (1 = resample-only stage)
        double outputRate;     // rate after this stage
    };
    std::vector<DecimStage> m_decimChain;
    double m_inputSampleRate;  // current HackRF sample rate
    bool m_audioCapable;       // false if carrier >= Nyquist

    void rebuildDecimationChain();

    // Audio filters (FIR) - final 15 kHz bandwidth filter at 48 kHz
    std::vector<float> m_audioFilterTaps;

    // Audio buffer
    std::vector<float> m_audioBuffer;

    // FM demodulator state (atan2 method)
    float m_lastPhase;

    // Frequency shift state
    double m_audioPhase;
    double m_audioPhaseIncrement;
    double m_currentCarrierFreq;   // actual carrier freq for freq shift (default AUDIO_CARRIER, updated by setAudioCarrierFreq)

    // Settings
    float m_audioGain;
    bool m_audioEnabled;
    double fmDeviation;

    // Helper functions
    void initFinalFilter();

    std::vector<float> designLowPassFIR(int numTaps, float cutoffFreq, float sampleRate);

    std::vector<float> applyFIRFilter(const std::vector<float>& signal,
                                      const std::vector<float>& coeffs);

    // Frequency shift
    std::vector<std::complex<float>> frequencyShift(
        const std::vector<std::complex<float>>& signal,
        double shiftFreq);

    // FM demodulation (atan2 method) - uses m_inputSampleRate for scaling
    std::vector<float> fmDemodulateAtan2(const std::vector<std::complex<float>>& signal);

    // FM demodulation for narrowband signal at a specific rate
    // Scaling: output +/-1.0 corresponds to +/-FM_DEVIATION
    std::vector<float> fmDemodulateNarrowband(const std::vector<std::complex<float>>& signal,
                                               double signalRate);

    // Phase unwrap
    float unwrapPhase(float phase, float lastPhase);

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
