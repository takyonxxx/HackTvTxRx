#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QWaitCondition>
#include <vector>
#include <atomic>
#include <deque>

class AudioOutput : public QObject
{
    Q_OBJECT
public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput();

    bool initializeAudio();
    void stop();

    int queueSize() const;
    double queueDuration() const;
    int sampleRate() const { return SAMPLE_RATE; }
    bool isRunning() const { return m_running.load(); }

public slots:
    void enqueueAudio(const std::vector<float>& samples);
    void setVolume(int value);

private slots:
    void handleAudioOutputStateChanged(QAudio::State newState);

private:
    void audioWriterLoop();
    void processAudioChunk(const std::vector<float>& audioData);

    // Audio configuration
    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int CHANNEL_COUNT = 2;
    static constexpr int MIN_BUFFER_SAMPLES = 28800;  // 600ms @ 48kHz
    static constexpr int CHUNK_SIZE = 2400;  // 50ms @ 48kHz
    static constexpr int MAX_QUEUE_SIZE = 480000;  // 10s

    // Audio format and device
    QAudioFormat m_format;
    QScopedPointer<QAudioSink> m_audioOutput;
    QIODevice* audioDevice = nullptr;

    // Thread-safe queue using deque
    std::deque<float> audioQueue;
    mutable QMutex mutex;
    QWaitCondition queueNotEmpty;

    // Worker thread
    QThread* audioWriterThread;
    std::atomic_bool m_running{true};

    // Pre-allocated conversion buffer
    std::vector<qint16> conversionBuffer;
};

#endif // AUDIOOUTPUT_H
