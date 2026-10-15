#include "TxEngine.h"

#include <QDebug>
#include <QElapsedTimer>
#include <chrono>

TxEngine::TxEngine(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_buf.resize(CHUNK_BYTES);
    m_iq.resize(CHUNK_BYTES / 2);
}

TxEngine::~TxEngine()
{
    stop();
}

bool TxEngine::start(SpscRing<int8_t>* ring, uint32_t sampleRate, ITxSource* source)
{
    if (m_running.load()) stop();
    if (!ring || !source) return false;

    m_ring = ring;
    m_source = source;
    m_sampleRate = sampleRate;
    m_ring->clear();
    // Keep ~m_latencySec of IQ queued: enough to ride out USB burstiness, small enough for PTT
    m_targetFill = std::min(m_ring->capacity() - CHUNK_BYTES,
                            static_cast<size_t>(static_cast<double>(sampleRate) * 2.0 * m_latencySec));
    m_targetFill = std::max(m_targetFill, CHUNK_BYTES * 4);

    if (!m_source->txStart(sampleRate)) {
        m_source = nullptr;
        return false;
    }

    m_running.store(true, std::memory_order_release);
    m_thread = std::thread([this] { loop(); });
    return true;
}

void TxEngine::stop()
{
    if (!m_running.exchange(false)) {
        if (m_thread.joinable()) m_thread.join();
        return;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
    if (m_source) m_source->txStop();
    m_source = nullptr;
    m_ring = nullptr;
}

bool TxEngine::waitPrefilled(int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    const size_t target = m_targetFill * 9 / 10;
    while (m_running.load() && m_ring->available() < target) {
        if (t.elapsed() > timeoutMs) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return m_running.load();
}

void TxEngine::notify()
{
    m_wake.notify_one();
}

void TxEngine::loop()
{
    QElapsedTimer fftTimer;
    fftTimer.start();
    bool endedReported = false;

    while (m_running.load(std::memory_order_acquire)) {
        if (m_ring->freeSpace() < CHUNK_BYTES || m_ring->available() >= m_targetFill) {
            std::unique_lock<std::mutex> lk(m_wakeMutex);
            m_wake.wait_for(lk, std::chrono::milliseconds(2), [this] {
                return !m_running.load(std::memory_order_acquire) ||
                       (m_ring->freeSpace() >= CHUNK_BYTES && m_ring->available() < m_targetFill);
            });
            continue;
        }

        const size_t got = m_source->txProduce(m_buf.data(), CHUNK_BYTES);
        if (got == 0) {
            if (!endedReported) { endedReported = true; emit sourceEnded(); }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        endedReported = false;
        m_ring->write(m_buf.data(), got);

        // TX monitor spectrum
        const int interval = 1000 / std::max(1, m_fftRate.load());
        if (fftTimer.elapsed() >= interval) {
            fftTimer.restart();
            const size_t n = got / 2;
            const int size = m_fft.size();
            if (static_cast<size_t>(size) <= n) {
                for (int i = 0; i < size; ++i)
                    m_iq[i] = std::complex<float>(m_buf[2 * i] * (1.0f / 128.0f), m_buf[2 * i + 1] * (1.0f / 128.0f));
                QVector<float> bins(size);
                const float level = m_fft.compute(m_iq.data(), bins.data());
                emit spectrumReady(bins, level);
            }
        }
    }
}
