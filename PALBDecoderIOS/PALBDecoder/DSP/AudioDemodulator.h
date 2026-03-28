#ifndef AUDIODEMODULATOR_H
#define AUDIODEMODULATOR_H

#include <vector>
#include <complex>
#include <cstdint>
#include <cmath>
#include <mutex>
#include <functional>
#include <algorithm>
#include <numeric>

class AudioDemodulator
{
public:
    enum class DemodMode {
        FM = 0,     // Frequency Modulation (default for PAL-B/G audio)
        AM = 1      // Amplitude Modulation (envelope detection)
    };

    AudioDemodulator();
    ~AudioDemodulator() = default;

    using AudioCallback = std::function<void(const float* samples, size_t count)>;
    void setAudioCallback(AudioCallback cb) { m_audioCallback = std::move(cb); }

    void processSamples(const int8_t* data, size_t len);
    void processSamples(const std::vector<std::complex<float>>& samples);

    void setAudioGain(float gain) { m_audioGain = gain; }
    void setAudioEnabled(bool enabled) { m_audioEnabled = enabled; }
    float getAudioGain() const { return m_audioGain; }
    bool getAudioEnabled() const { return m_audioEnabled; }
    bool isAudioCapable() const { return m_audioCapable; }

    void setSampleRate(double newSampleRate);
    double getSampleRate() const { return m_inputSampleRate; }
    void setAudioCarrierFreq(double freqHz);

    // Radio mode: baseband wideband FM (75 kHz deviation, no carrier shift)
    void setRadioMode(bool radio);
    bool isRadioMode() const { return m_radioMode; }

    // Demodulation mode: FM or AM
    void setDemodMode(DemodMode mode);
    DemodMode getDemodMode() const { return m_demodMode; }

private:
    static constexpr int AUDIO_SAMP_RATE = 48000;
    static constexpr int AUDIO_BUFFER_SIZE = 480;
    static constexpr double AUDIO_CARRIER = 5.5e6;
    static constexpr double FM_DEVIATION_TV = 50e3;
    static constexpr double FM_DEVIATION_RADIO = 75e3;
    static constexpr int FILTER_TAPS = 17;

    std::mutex m_processMutex;
    std::mutex m_phaseMutex;
    AudioCallback m_audioCallback;

    struct DecimStage {
        std::vector<float> filterTaps;
        int decimFactor;
        double outputRate;
    };
    std::vector<DecimStage> m_decimChain;
    double m_inputSampleRate;
    bool m_audioCapable;
    bool m_radioMode;
    DemodMode m_demodMode;

    void rebuildDecimationChain();

    std::vector<float> m_audioFilterTaps;
    std::vector<float> m_audioBuffer;
    float m_lastPhase;
    double m_audioPhase;
    double m_audioPhaseIncrement;
    double m_currentCarrierFreq;
    float m_audioGain;
    bool m_audioEnabled;
    double fmDeviation;

    // AM demod state
    float m_amDcState;      // DC removal filter state
    float m_amAgcLevel;     // AGC peak level for AM normalization

    void initFinalFilter();
    std::vector<float> designLowPassFIR(int numTaps, float cutoffFreq, float sampleRate);
    void applyFIRFilterInPlace(std::vector<float>& signal, const std::vector<float>& coeffs);
    std::vector<std::complex<float>> frequencyShift(const std::vector<std::complex<float>>& signal, double shiftFreq);
    void fmDemodulateInPlace(const std::vector<std::complex<float>>& signal, std::vector<float>& out, double signalRate);
    float unwrapPhase(float phase, float lastPhase);
    void decimateInPlace(std::vector<float>& signal, int factor);
    void resampleInPlace(std::vector<float>& signal, double inputRate, double outputRate);
    void emitAudioBuffer(const std::vector<float>& audio);

    // Pre-allocated work buffers (avoid per-call malloc)
    std::vector<float> m_workReal, m_workImag, m_workAudio;
    std::vector<float> m_firTemp;
};

#endif
