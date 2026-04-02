#ifndef AUDIOPLAYBACK_H
#define AUDIOPLAYBACK_H

#include <QObject>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <vector>
#include <atomic>
#include <memory>

class AudioPlayback : public QObject
{
    Q_OBJECT

public:
    explicit AudioPlayback(QObject *parent = nullptr);
    ~AudioPlayback();

    bool start();
    void stop();
    void enqueueAudio(const std::vector<float>& samples);
    bool isRunning() const { return m_running.load(); }

    void setVolume(float vol); // 0.0 - 1.0

private:
    bool initAudioSink();
    void writerLoop();
    void writeChunk(const std::vector<float>& chunk);

    QAudioFormat m_format;
    std::unique_ptr<QAudioSink> m_audioSink;
    QIODevice* m_ioDevice = nullptr;

    // Ring buffer - large for TCP jitter absorption
    static constexpr size_t RING_SIZE = 480000;          // 10s at 48kHz
    static constexpr size_t CHUNK_SIZE = 1024;           // samples per write
    static constexpr size_t MIN_BUFFER_SAMPLES = 2400;   // 50ms priming
    static constexpr size_t MAX_QUEUE_SIZE = 240000;     // 5s max
    static constexpr int SAMPLE_RATE = 48000;

    std::vector<float> m_ring;
    size_t m_writePos = 0;
    size_t m_readPos = 0;
    size_t m_bufferSize = 0;

    mutable QMutex m_mutex;
    QWaitCondition m_queueNotEmpty;

    std::atomic<bool> m_running{false};
    std::atomic<float> m_volume{0.5f};

    QThread m_writerThread;
    QByteArray m_outputBuffer;
};

#endif // AUDIOPLAYBACK_H
