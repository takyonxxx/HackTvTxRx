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

    void initFinalFilter();
    std::vector<float> designLowPassFIR(int numTaps, float cutoffFreq, float sampleRate);
    std::vector<float> applyFIRFilter(const std::vector<float>& signal, const std::vector<float>& coeffs);
    std::vector<std::complex<float>> frequencyShift(const std::vector<std::complex<float>>& signal, double shiftFreq);
    std::vector<float> fmDemodulateNarrowband(const std::vector<std::complex<float>>& signal, double signalRate);
    float unwrapPhase(float phase, float lastPhase);
    std::vector<float> decimate(const std::vector<float>& signal, int factor);
    std::vector<float> resample(const std::vector<float>& signal, double inputRate, double outputRate);
    void emitAudioBuffer(const std::vector<float>& audio);
};

#endif
