#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QBuffer>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QIODevice>
#include <QWaitCondition>
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

public slots:
    void setVolume(int value);
    void handleAudioOutputStateChanged(QAudio::State newState);

private:
    void processAudio(const std::vector<float>& audioData);
    void audioWriterLoop();

    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int CHANNEL_COUNT = 2;
    static constexpr int SAMPLE_SIZE = 16;

    QMutex mutex;
    QWaitCondition queueNotEmpty;

    QQueue<float> audioQueue;
    std::atomic_bool m_running {true};

    QAudioFormat m_format;
    QAudioFormat m_inputFormat;
    QScopedPointer<QAudioSink> m_audioOutput;
    QIODevice* audioDevice{nullptr};
    QByteArray buffer;

    QThread audioWriterThread;
};

#endif // AUDIOOUTPUT_H
