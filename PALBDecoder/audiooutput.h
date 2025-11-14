#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <vector>
#include <atomic>

class AudioOutput : public QObject
{
    Q_OBJECT
public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput();

    bool initializeAudio();
    void stop();
    void enqueueAudio(const std::vector<float>& samples);

    int queueSize() const;
    double queueDuration() const;
    int sampleRate() const { return m_format.sampleRate(); }
    bool isRunning() const { return m_running.load(); }

public slots:
    void setVolume(int value);

private slots:
    void handleAudioOutputStateChanged(QAudio::State newState);

private:
    void audioWriterLoop();
    void processAudio(const std::vector<float>& audioData);

    // Audio configuration
    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int CHANNEL_COUNT = 2;

    // Buffer configuration (optimized for stability)
    static constexpr int MIN_BUFFER_SAMPLES = 4800;    // 200ms @ 48kHz (priming)
    static constexpr int RESTART_THRESHOLD = 2400;     // 100ms (re-prime threshold)
    static constexpr int CHUNK_SIZE = 480;              // 10ms @ 48kHz (write chunks)
    static constexpr int MAX_QUEUE_SIZE = 480000;       // 10s maximum
    static constexpr int RESERVE_SIZE = 500000;         // Pre-allocated size

    // Audio format and device
    QAudioFormat m_format;
    QAudioFormat m_inputFormat;
    QScopedPointer<QAudioSink> m_audioOutput;
    QIODevice* audioDevice = nullptr;

    // High-performance circular buffer
    std::vector<float> audioBuffer;
    size_t writePos = 0;
    size_t readPos = 0;
    size_t bufferSize = 0;

    // Thread synchronization
    mutable QMutex mutex;
    QWaitCondition queueNotEmpty;

    // Worker thread
    QThread audioWriterThread;
    std::atomic_bool m_running{true};

    // Pre-allocated output buffer
    QByteArray outputBuffer;
};

#endif // AUDIOOUTPUT_H
