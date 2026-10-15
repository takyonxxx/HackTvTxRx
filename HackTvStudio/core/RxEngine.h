#ifndef RXENGINE_H
#define RXENGINE_H

#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>
#include <complex>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdint>

#include "SpscRing.h"
#include "Fft.h"

/*
 * A mode-specific consumer of baseband IQ (PAL decoder, FM/AM demodulator...).
 * All three methods are called on the RxEngine thread.
 */
class IRxSink
{
public:
    virtual ~IRxSink() = default;
    virtual void sinkStart(uint32_t sampleRate) { (void)sampleRate; }
    virtual void sinkProcess(const std::complex<float>* iq, size_t count) = 0;
    virtual void sinkStop() {}
};

/*
 * The RX DSP thread.
 *
 *   USB thread -> DeviceController::rxRing() -> [RxEngine thread]
 *       int8 -> complex<float>  (one reusable buffer, no per-chunk allocation)
 *       spectrum (FFT at <= fftRate Hz) -> spectrumReady() to the GUI
 *       optional raw IQ recording
 *       sink->sinkProcess()    -> the active mode's pipeline
 *
 * The engine never allocates or blocks on the GUI; the GUI never does DSP.
 */
class RxEngine : public QObject
{
    Q_OBJECT
public:
    explicit RxEngine(QObject* parent = nullptr);
    ~RxEngine() override;

    void start(SpscRing<int8_t>* ring, uint32_t sampleRate);
    void stop();
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

    /* Wake the engine thread (called by the producer after each write). */
    void notify();

    /* The sink is NOT owned. Pass nullptr before destroying a sink. Thread-safe. */
    void setSink(IRxSink* sink);

    void setSampleRate(uint32_t sampleRate);
    void setFftSize(int size);        // power of two, default 2048
    void setFftRate(int framesPerSec); // default 25

    /* Raw int8 IQ capture on the engine thread; stops itself at maxBytes. */
    bool startRecording(const QString& path, int64_t maxBytes);
    void stopRecording();
    bool isRecording() const { return m_recording.load(std::memory_order_acquire); }

    /* Fraction of int8 samples at full scale (+127/-128), smoothed. > ~0.1% means ADC overload. */
    float clipRatio() const { return m_clipRatio.load(std::memory_order_relaxed); }

signals:
    void spectrumReady(const QVector<float>& dbBins, float levelDbfs);
    void recordingFinished(const QString& path, qint64 bytes);

private:
    void loop();
    void finishRecording();

    SpscRing<int8_t>* m_ring = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::mutex m_wakeMutex;
    std::condition_variable m_wake;

    std::mutex m_sinkMutex;
    IRxSink* m_sink = nullptr;
    std::atomic<uint32_t> m_sampleRate{16000000};
    std::atomic<bool> m_sinkNeedsStart{false};

    SpectrumAnalyzer m_fft{2048};
    std::mutex m_fftMutex;
    std::atomic<int> m_fftRate{25};
    QVector<float> m_fftOut;

    std::atomic<float> m_clipRatio{0.0f};
    std::atomic<bool> m_recording{false};
    std::ofstream m_recFile;
    QString m_recPath;
    int64_t m_recMax = 0;
    int64_t m_recBytes = 0;
    std::mutex m_recMutex;

    // 262144 bytes = 131072 IQ samples = 8.2 ms at 16 MS/s
    static constexpr size_t CHUNK_BYTES = 262144;
    std::vector<int8_t> m_raw;
    std::vector<std::complex<float>> m_iq;
};

#endif // RXENGINE_H
