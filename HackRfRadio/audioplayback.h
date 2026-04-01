#ifndef AUDIOPLAYBACK_H
#define AUDIOPLAYBACK_H

#include <QObject>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <vector>
#include <atomic>

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

private slots:
    void processAudioQueue();

private:
    QAudioFormat m_format;
    QAudioSink* m_audioSink = nullptr;
    QIODevice* m_ioDevice = nullptr;

    // Ring buffer
    static constexpr size_t RING_SIZE = 480000; // 10 seconds at 48kHz
    std::vector<float> m_ring;
    std::atomic<size_t> m_writePos{0};
    std::atomic<size_t> m_readPos{0};

    std::atomic<bool> m_running{false};
    std::atomic<float> m_volume{0.5f};

    QTimer* m_writeTimer = nullptr;
};

#endif // AUDIOPLAYBACK_H
