#ifndef TXENGINE_H
#define TXENGINE_H

#include <QObject>
#include <QVector>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdint>

#include "SpscRing.h"
#include "Fft.h"

/*
 * A producer of transmit baseband: FM/AM modulator, hacktv video encoder...
 * All three methods are called on the TxEngine thread.
 */
class ITxSource
{
public:
    virtual ~ITxSource() = default;
    virtual bool   txStart(uint32_t sampleRate) = 0;
    /* Fill `bytes` bytes of interleaved int8 IQ. Return the number of bytes
     * written (normally == bytes); 0 means "nothing right now, try again". */
    virtual size_t txProduce(int8_t* iq, size_t bytes) = 0;
    virtual void   txStop() {}
};

/*
 * The TX DSP thread.
 *
 *   [TxEngine thread] source->txProduce() -> DeviceController::txRing() -> USB thread (memcpy only)
 *
 * Keeps the ring topped up (the USB callback never computes anything) and
 * publishes a spectrum of what is being transmitted (TX monitor).
 */
class TxEngine : public QObject
{
    Q_OBJECT
public:
    explicit TxEngine(QObject* parent = nullptr);
    ~TxEngine() override;

    /* Starts the source and the thread; returns false if the source refused. */
    bool start(SpscRing<int8_t>* ring, uint32_t sampleRate, ITxSource* source);
    void stop();
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

    /* Block until the ring holds its target fill (or timeout). */
    bool waitPrefilled(int timeoutMs);
    /* Target queue depth in seconds (default 0.25 s, capped by the ring size). */
    void setLatencySeconds(double s) { m_latencySec = s; }

    /* Wake the thread (called by the USB consumer after it drained data). */
    void notify();

    void setFftRate(int framesPerSec) { m_fftRate.store(framesPerSec); }

signals:
    void spectrumReady(const QVector<float>& dbBins, float levelDbfs);
    void sourceEnded();        // the source reported end of stream (e.g. video file finished)

private:
    void loop();

    SpscRing<int8_t>* m_ring = nullptr;
    ITxSource* m_source = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::mutex m_wakeMutex;
    std::condition_variable m_wake;
    uint32_t m_sampleRate = 2000000;
    size_t m_targetFill = 0;      // bytes kept queued ahead of the USB thread (= TX latency)

    SpectrumAnalyzer m_fft{2048};
    std::atomic<int> m_fftRate{25};
    double m_latencySec = 0.25;

    static constexpr size_t CHUNK_BYTES = 65536;   // 32768 IQ samples
    std::vector<int8_t> m_buf;
    std::vector<std::complex<float>> m_iq;
};

#endif // TXENGINE_H
