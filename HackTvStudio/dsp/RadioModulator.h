#ifndef RADIOMODULATOR_H
#define RADIOMODULATOR_H

#include "core/TxEngine.h"
#include "core/SpscRing.h"
#include "hackrf/modulation.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

/*
 * Audio -> baseband IQ for the radio transmitter.
 *
 *   GUI thread: pushAudio()  (microphone, audio file or test tone, 48 kHz stereo float)
 *       -> SpscRing<float>
 *   TX thread: txProduce()   WFM: stereo MPX (19 kHz pilot, 38 kHz DSB-SC, pre-emphasis)
 *                            NFM: mono, pre-emphasis, narrow deviation
 *                            AM : DSB-AM envelope
 *                            -> FM/AM modulate at the HackRF sample rate -> int8 IQ
 *
 * Audio that is not there yet (ring empty) becomes silence: the carrier stays
 * on, unmodulated. Nothing is ever discarded, so there are no clicks: baseband
 * left over from one call is carried into the next.
 */
class RadioModulator : public ITxSource
{
public:
    enum class Mode { WFM, NFM, AM };

    static constexpr uint32_t AUDIO_RATE = 48000;

    RadioModulator();
    ~RadioModulator() override;

    // ---- parameters (GUI thread, applied live)
    void setMode(Mode m)             { m_mode.store(static_cast<int>(m)); m_reconfigure.store(true); }
    void setDeviationHz(float hz)    { m_deviationHz.store(hz); }
    void setAmDepth(float depth)     { m_amDepth.store(depth); }       // 0..1.2
    void setAmplitude(float a)       { m_amplitude.store(a); }         // 0..1 of full scale
    void setPreemphasisUs(float us)  { m_preemphUs.store(us); m_reconfigure.store(true); }
    void setAudioGain(float g)       { m_audioGain.store(g); }
    void setAgc(bool on)             { m_agc.store(on); }

    // ---- audio feed (GUI thread): interleaved stereo float, 48 kHz
    void   pushAudio(const float* stereo, size_t floats);

    /* Pull source (file player, tone...): called ON THE TX THREAD whenever the
     * ring cannot supply a block. Must fill up to `frames` stereo frames and
     * return how many it wrote. Set before txStart(); nullptr = ring only. */
    using PullFn = std::function<size_t(float* stereo, size_t frames)>;
    void   setPullSource(PullFn fn) { m_pull = std::move(fn); }
    size_t queuedFrames() const { return m_audio.available() / 2; }
    void   clearAudio() { m_audio.clear(); }

    // ---- monitoring
    float    inputLevelDb() const { return m_inputDb.load(); }
    uint64_t underrunFrames() const { return m_underrun.load(); }
    bool     transmitting() const { return m_running.load(); }

    // ---- ITxSource (TX thread)
    bool   txStart(uint32_t sampleRate) override;
    size_t txProduce(int8_t* iq, size_t bytes) override;
    void   txStop() override;

private:
    void reconfigure();
    void generateBaseband(size_t wantedTxSamples);

    SpscRing<float> m_audio{1u << 20};   // 512k stereo frames ~ 10 s
    PullFn m_pull;

    std::atomic<int>   m_mode{0};
    std::atomic<float> m_deviationHz{75000.0f};
    std::atomic<float> m_amDepth{0.8f};
    std::atomic<float> m_amplitude{0.5f};
    std::atomic<float> m_preemphUs{50.0f};
    std::atomic<float> m_audioGain{1.0f};
    std::atomic<bool>  m_agc{true};
    std::atomic<bool>  m_reconfigure{true};
    std::atomic<bool>  m_running{false};
    std::atomic<float> m_inputDb{-100.0f};
    std::atomic<uint64_t> m_underrun{0};

    uint32_t m_rate = 2000000;
    Mode m_curMode = Mode::WFM;
    std::unique_ptr<StereoMPXGenerator> m_mpx;

    // NFM / AM mono path state
    float  m_preemphPrev = 0.0f;
    float  m_agcGain = 1.0f;
    double m_resamplePos = 0.0;      // fractional position into the mono audio block
    float  m_lastMono = 0.0f;

    // FM/AM modulator state
    double m_phase = 0.0;

    std::vector<float> m_stereoChunk;    // audio pulled from the ring
    std::vector<float> m_baseband;       // baseband at TX rate waiting to be modulated
    size_t m_basebandPos = 0;
};

#endif // RADIOMODULATOR_H
