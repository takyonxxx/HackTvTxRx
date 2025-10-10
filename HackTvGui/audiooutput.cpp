#include "audiooutput.h"
#include <QMediaDevices>
#include <QDebug>
#include <algorithm>

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
{
    initializeAudio();

    // Audio writer thread'i başlat
    moveToThread(&audioWriterThread);
    connect(&audioWriterThread, &QThread::started, this, &AudioOutput::audioWriterLoop);
    audioWriterThread.start();
}

AudioOutput::~AudioOutput()
{
    m_running = false;
    queueNotEmpty.wakeAll();
    audioWriterThread.quit();
    audioWriterThread.wait();

    if (audioDevice) {
        audioDevice->close();
        audioDevice = nullptr;
    }

    if (m_audioOutput) {
        m_audioOutput->stop();
        m_audioOutput.reset();
    }
}

bool AudioOutput::initializeAudio()
{
    try {
        QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
        if (defaultDevice.isNull()) {
            qDebug() << "No default audio output device available";
            return false;
        }

        m_format = defaultDevice.preferredFormat();
        m_format.setSampleRate(50e3f);
        m_format.setChannelCount(2);
        m_format.setSampleFormat(QAudioFormat::Int16);
        m_inputFormat = m_format;

        if (!defaultDevice.isFormatSupported(m_format)) {
            qDebug() << "Format not supported!";
            return false;
        }

        m_audioOutput.reset(new QAudioSink(defaultDevice, m_format));
        if (!m_audioOutput) return false;

        m_audioOutput->setBufferSize(1024 * 1024);
        connect(m_audioOutput.get(), &QAudioSink::stateChanged,
                this, &AudioOutput::handleAudioOutputStateChanged);

        audioDevice = m_audioOutput->start();
        if (!audioDevice || !audioDevice->isOpen()) {
            qDebug() << "Failed to start audio device";
            return false;
        }

        qDebug() << "Audio initialized successfully";
        return true;
    } catch (...) {
        return false;
    }
}

void AudioOutput::handleAudioOutputStateChanged(QAudio::State newState)
{
    Q_UNUSED(newState)
}

void AudioOutput::setVolume(int value)
{
    if (m_audioOutput) {
        qreal linearVolume = value / 100.0;
        qreal volume = QAudio::convertVolume(linearVolume,
                                             QAudio::LinearVolumeScale,
                                             QAudio::LogarithmicVolumeScale);
        m_audioOutput->setVolume(volume);
    }
}

void AudioOutput::enqueueAudio(const std::vector<float>& samples)
{
    if (samples.empty()) return;

    QMutexLocker locker(&mutex);
    for (float s : samples) {
        audioQueue.enqueue(s);
    }
    queueNotEmpty.wakeOne();
}

void AudioOutput::audioWriterLoop()
{
    while (m_running) {
        std::vector<float> chunk;
        {
            QMutexLocker locker(&mutex);
            while (audioQueue.isEmpty() && m_running)
                queueNotEmpty.wait(&mutex);

            int n = std::min<int>(audioQueue.size(), 4096); // chunk size
            chunk.reserve(n);
            for (int i = 0; i < n; ++i) {
                chunk.push_back(audioQueue.dequeue());
            }
        }

        if (!chunk.empty())
            processAudio(chunk);
    }
}

void AudioOutput::processAudio(const std::vector<float>& audioData)
{
    if (!audioDevice || audioData.empty()) return;

    // Mono float -> stereo int16
    buffer.resize(audioData.size() * 2 * sizeof(qint16));
    qint16* output = reinterpret_cast<qint16*>(buffer.data());

    for (size_t i = 0; i < audioData.size(); ++i) {
        float clamped = std::clamp(audioData[i], -1.0f, 1.0f);
        qint16 sample = static_cast<qint16>(clamped * 32767.0f);
        output[i*2] = sample;      // Left
        output[i*2 + 1] = sample;  // Right
    }

    if (m_audioOutput->bytesFree() >= buffer.size()) {
        audioDevice->write(buffer);
    }
}
