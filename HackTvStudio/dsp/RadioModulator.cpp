#include "RadioModulator.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr size_t AUDIO_BLOCK_FRAMES = 480;   // 10 ms at 48 kHz
}

RadioModulator::RadioModulator() = default;
RadioModulator::~RadioModulator() = default;

void RadioModulator::pushAudio(const float* stereo, size_t floats)
{
    if (!stereo || floats < 2) return;
    floats &= ~static_cast<size_t>(1);
    m_audio.write(stereo, floats);
}

bool RadioModulator::txStart(uint32_t sampleRate)
{
    m_rate = sampleRate;
    m_audio.clear();
    m_baseband.clear();
    m_basebandPos = 0;
    m_phase = 0.0;
    m_resamplePos = 0.0;
    m_lastMono = 0.0f;
    m_preemphPrev = 0.0f;
    m_agcGain = 1.0f;
    m_underrun.store(0);
    m_reconfigure.store(true);
    reconfigure();
    m_running.store(true);
    return true;
}

void RadioModulator::txStop()
{
    m_running.store(false);
    m_audio.clear();
}

void RadioModulator::reconfigure()
{
    m_curMode = static_cast<Mode>(m_mode.load());
    const float tau = m_preemphUs.load() * 1e-6f;
    if (m_curMode == Mode::WFM)
        m_mpx = std::make_unique<StereoMPXGenerator>(static_cast<float>(AUDIO_RATE), static_cast<float>(m_rate), tau);
    else
        m_mpx.reset();
    m_baseband.clear();
    m_basebandPos = 0;
    m_reconfigure.store(false);
}

/* Pull audio from the ring (silence on underrun) until at least
 * `wantedTxSamples` baseband samples are waiting in m_baseband. */
void RadioModulator::generateBaseband(size_t wantedTxSamples)
{
    const float ratio = static_cast<float>(m_rate) / static_cast<float>(AUDIO_RATE);
    const float gain = m_audioGain.load();
    const bool agc = m_agc.load();

    while (m_baseband.size() - m_basebandPos < wantedTxSamples) {
        // Compact the carry buffer
        if (m_basebandPos > 0) {
            m_baseband.erase(m_baseband.begin(), m_baseband.begin() + static_cast<long>(m_basebandPos));
            m_basebandPos = 0;
        }

        m_stereoChunk.assign(AUDIO_BLOCK_FRAMES * 2, 0.0f);
        size_t got = m_audio.read(m_stereoChunk.data(), m_stereoChunk.size());
        if (got < m_stereoChunk.size() && m_pull) {
            // Pull the rest straight from the source on this thread (no GUI timer involved)
            got &= ~static_cast<size_t>(1);
            got += 2 * m_pull(m_stereoChunk.data() + got, (m_stereoChunk.size() - got) / 2);
        }
        if (got < m_stereoChunk.size()) {
            std::fill(m_stereoChunk.begin() + static_cast<long>(got), m_stereoChunk.end(), 0.0f);
            m_underrun.fetch_add((m_stereoChunk.size() - got) / 2, std::memory_order_relaxed);
        }

        // Gain + optional AGC (slow attack / fast release on the peak)
        float peak = 0.0f;
        for (float& s : m_stereoChunk) { s *= gain; peak = std::max(peak, std::fabs(s)); }
        {
            // Input level for the VU meter (after gain, before AGC), fast up / slow down
            const float db = 20.0f * std::log10(std::max(peak, 1e-5f));
            const float prev = m_inputDb.load(std::memory_order_relaxed);
            m_inputDb.store(db > prev ? db : prev + 0.1f * (db - prev), std::memory_order_relaxed);
        }
        if (agc) {
            if (peak > 0.005f) {
                const float desired = 0.8f / peak;
                if (desired < m_agcGain) m_agcGain = m_agcGain * 0.8f + desired * 0.2f;
                else                     m_agcGain = m_agcGain * 0.995f + desired * 0.005f;
                m_agcGain = std::min(m_agcGain, 50.0f);
            }
            for (float& s : m_stereoChunk) s = std::clamp(s * m_agcGain, -1.0f, 1.0f);
        } else {
            for (float& s : m_stereoChunk) s = std::clamp(s, -1.0f, 1.0f);
        }

        if (m_curMode == Mode::WFM && m_mpx) {
            // Stereo multiplex at the TX rate (pilot + DSB-SC + pre-emphasis inside)
            std::vector<float> mpx = m_mpx->process(m_stereoChunk.data(), m_stereoChunk.size());
            m_baseband.insert(m_baseband.end(), mpx.begin(), mpx.end());
        } else {
            // Mono path: L+R, (pre-emphasis for NFM), linear upsample to the TX rate
            const bool preemph = (m_curMode == Mode::NFM) && m_preemphUs.load() > 0.0f;
            const float k = m_preemphUs.load() * 1e-6f * static_cast<float>(AUDIO_RATE);   // tau * fs
            std::vector<float> mono(AUDIO_BLOCK_FRAMES);
            for (size_t i = 0; i < AUDIO_BLOCK_FRAMES; ++i) {
                float m = 0.5f * (m_stereoChunk[2 * i] + m_stereoChunk[2 * i + 1]);
                if (preemph) {
                    // y = x + tau*fs*(x - x_prev): +6 dB/oct above 1/(2*pi*tau), soft-limited
                    const float y = m + k * (m - m_preemphPrev);
                    m_preemphPrev = m;
                    m = std::tanh(y);
                }
                mono[i] = m;
            }
            // Resample 48 kHz -> TX rate with a continuous fractional position
            const double step = 1.0 / ratio;
            double pos = m_resamplePos;
            while (pos < static_cast<double>(AUDIO_BLOCK_FRAMES)) {
                const int idx = static_cast<int>(pos);
                const float frac = static_cast<float>(pos - idx);
                const float a = (idx == 0) ? m_lastMono : mono[idx - 1];
                const float b = mono[idx];
                m_baseband.push_back(a + (b - a) * frac);
                pos += step;
            }
            m_resamplePos = pos - static_cast<double>(AUDIO_BLOCK_FRAMES);
            m_lastMono = mono.back();
        }
    }
}

size_t RadioModulator::txProduce(int8_t* iq, size_t bytes)
{
    if (m_reconfigure.load()) reconfigure();

    const size_t n = bytes / 2;
    generateBaseband(n);

    const float amp = std::clamp(m_amplitude.load(), 0.0f, 1.0f) * 127.0f;
    const float* bb = m_baseband.data() + m_basebandPos;

    if (m_curMode == Mode::AM) {
        const float depth = m_amDepth.load();
        for (size_t i = 0; i < n; ++i) {
            float env = 1.0f + depth * bb[i];
            if (env < 0.0f) env = 0.0f;
            const float I = std::min(env * amp, 127.0f);
            iq[2 * i]     = static_cast<int8_t>(I);
            iq[2 * i + 1] = 0;
        }
    } else {
        // FM: phase advance per sample = 2*pi*deviation*x/fs
        const double k = 2.0 * kPi * static_cast<double>(m_deviationHz.load()) / static_cast<double>(m_rate);
        double ph = m_phase;
        for (size_t i = 0; i < n; ++i) {
            ph += k * bb[i];
            if (ph > kPi)       ph -= 2.0 * kPi;
            else if (ph < -kPi) ph += 2.0 * kPi;
            iq[2 * i]     = static_cast<int8_t>(std::cos(ph) * amp);
            iq[2 * i + 1] = static_cast<int8_t>(std::sin(ph) * amp);
        }
        m_phase = ph;
    }

    m_basebandPos += n;
    return bytes;
}
