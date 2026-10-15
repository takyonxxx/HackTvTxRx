#include "RxEngine.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMetaType>
#include <chrono>

RxEngine::RxEngine(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_raw.resize(CHUNK_BYTES);
    m_iq.resize(CHUNK_BYTES / 2);
}

RxEngine::~RxEngine()
{
    stop();
}

void RxEngine::start(SpscRing<int8_t>* ring, uint32_t sampleRate)
{
    if (m_running.load()) stop();

    m_ring = ring;
    m_sampleRate.store(sampleRate);
    m_sinkNeedsStart.store(true);
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread([this] { loop(); });
}

void RxEngine::stop()
{
    if (!m_running.exchange(false)) {
        if (m_thread.joinable()) m_thread.join();
        return;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();

    {
        std::lock_guard<std::mutex> lk(m_sinkMutex);
        if (m_sink) m_sink->sinkStop();
    }
    finishRecording();
    m_ring = nullptr;
}

void RxEngine::notify()
{
    m_wake.notify_one();
}

void RxEngine::setSink(IRxSink* sink)
{
    std::lock_guard<std::mutex> lk(m_sinkMutex);
    if (m_sink == sink) return;
    if (m_sink && m_running.load()) m_sink->sinkStop();
    m_sink = sink;
    m_sinkNeedsStart.store(sink != nullptr);
}

void RxEngine::setSampleRate(uint32_t sampleRate)
{
    m_sampleRate.store(sampleRate);
    // The sink is re-armed with the new rate on the next chunk
    std::lock_guard<std::mutex> lk(m_sinkMutex);
    if (m_sink) m_sinkNeedsStart.store(true);
}

void RxEngine::setFftSize(int size)
{
    std::lock_guard<std::mutex> lk(m_fftMutex);
    m_fft.setSize(size);
}

void RxEngine::setFftRate(int framesPerSec)
{
    m_fftRate.store(std::max(1, std::min(framesPerSec, 60)));
}

bool RxEngine::startRecording(const QString& path, int64_t maxBytes)
{
    std::lock_guard<std::mutex> lk(m_recMutex);
    if (m_recording.load()) return false;
    m_recFile.open(path.toStdString(), std::ios::binary | std::ios::trunc);
    if (!m_recFile.is_open()) return false;
    m_recPath = path;
    m_recMax = maxBytes;
    m_recBytes = 0;
    m_recording.store(true, std::memory_order_release);
    return true;
}

void RxEngine::stopRecording()
{
    finishRecording();
}

void RxEngine::finishRecording()
{
    std::lock_guard<std::mutex> lk(m_recMutex);
    if (!m_recording.exchange(false)) return;
    m_recFile.close();
    emit recordingFinished(m_recPath, static_cast<qint64>(m_recBytes));
}

void RxEngine::loop()
{
    QElapsedTimer fftTimer;
    fftTimer.start();

    while (m_running.load(std::memory_order_acquire)) {
        // Wait for a full chunk (or stop). 5 ms timeout guards against a lost wake-up.
        {
            std::unique_lock<std::mutex> lk(m_wakeMutex);
            m_wake.wait_for(lk, std::chrono::milliseconds(5), [this] {
                return !m_running.load(std::memory_order_acquire) ||
                       (m_ring && m_ring->available() >= CHUNK_BYTES);
            });
        }
        if (!m_running.load(std::memory_order_acquire)) break;
        if (!m_ring || m_ring->available() < CHUNK_BYTES) continue;

        const size_t got = m_ring->read(m_raw.data(), CHUNK_BYTES);
        const size_t n = got / 2;
        if (n == 0) continue;

        // Raw capture
        if (m_recording.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lk(m_recMutex);
            if (m_recording.load()) {
                m_recFile.write(reinterpret_cast<const char*>(m_raw.data()), static_cast<std::streamsize>(got));
                m_recBytes += static_cast<int64_t>(got);
            }
            if (m_recBytes >= m_recMax) {
                // Can't call finishRecording() (re-locks); do it inline
                m_recording.store(false);
                m_recFile.close();
                emit recordingFinished(m_recPath, static_cast<qint64>(m_recBytes));
            }
        }

        // int8 -> float, one pass, no allocation; count full-scale samples for the overload indicator
        const int8_t* raw = m_raw.data();
        std::complex<float>* iq = m_iq.data();
        size_t clipped = 0;
        for (size_t i = 0; i < n; ++i) {
            const int8_t a = raw[2 * i], b = raw[2 * i + 1];
            clipped += (a == 127 || a == -128 || b == 127 || b == -128) ? 1 : 0;
            iq[i] = std::complex<float>(a * (1.0f / 128.0f), b * (1.0f / 128.0f));
        }
        {
            const float ratio = static_cast<float>(clipped) / static_cast<float>(n);
            const float prev = m_clipRatio.load(std::memory_order_relaxed);
            m_clipRatio.store(prev + 0.2f * (ratio - prev), std::memory_order_relaxed);
        }

        // Spectrum, throttled to m_fftRate frames per second
        const int interval = 1000 / m_fftRate.load();
        if (fftTimer.elapsed() >= interval) {
            fftTimer.restart();
            std::lock_guard<std::mutex> lk(m_fftMutex);
            const int size = m_fft.size();
            if (static_cast<size_t>(size) <= n) {
                QVector<float> bins(size);
                const float level = m_fft.compute(iq, bins.data());
                emit spectrumReady(bins, level);   // queued to the GUI thread
            }
        }

        // Mode pipeline
        {
            std::lock_guard<std::mutex> lk(m_sinkMutex);
            if (m_sink) {
                if (m_sinkNeedsStart.exchange(false))
                    m_sink->sinkStart(m_sampleRate.load());
                m_sink->sinkProcess(iq, n);
            }
        }
    }
}
