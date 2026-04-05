#ifndef AUDIOCAPTURE_H
#define AUDIOCAPTURE_H

#include <QObject>
#include <QAudioSource>
#include <QAudioFormat>
#include <QIODevice>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QTimer>
#include <vector>
#include <atomic>
#include <mutex>

class AudioCapture : public QObject
{
    Q_OBJECT

public:
    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture();

    bool start();
    void stop();
    bool isRunning() const { return m_running.load(); }

signals:
    void audioDataReady(const std::vector<float>& samples);

private slots:
    void onReadyRead();
    void onFlushTimer();

private:
    QAudioFormat m_format;
    QAudioSource* m_audioSource = nullptr;
    QIODevice* m_ioDevice = nullptr;
    QTimer* m_flushTimer = nullptr;
    std::atomic<bool> m_running{false};

    std::vector<float> m_accumulator;
    std::mutex m_accMutex;

    int m_inputChannels = 1;
    QAudioFormat::SampleFormat m_inputSampleFormat = QAudioFormat::Int16;

    static constexpr size_t SEND_THRESHOLD = 480; // 10ms at 48000 Hz
};

#endif // AUDIOCAPTURE_H
