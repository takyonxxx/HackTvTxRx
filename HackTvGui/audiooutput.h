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

class AudioOutput: public QObject
{
    Q_OBJECT
public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput();
    void stop();
    void writeBuffer(const QByteArray &buffer);    
public slots:
    void setVolume(int value);
    void handleAudioOutputStateChanged(QAudio::State newState);
    void processAudio(const std::vector<float>& audioData);
signals:
    void volumeChanged(int value);
private:

    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int CHANNEL_COUNT = 2;
    static constexpr int SAMPLE_SIZE = 16;

    QMutex *mutex{};
    bool m_abort {false};

    QAudioFormat m_format;
    QAudioFormat m_inputFormat;
    QScopedPointer<QAudioSink> m_audioOutput;
    QIODevice *audioDevice;
    QByteArray buffer;
};

#endif // AUDIOOUTPUT_H
