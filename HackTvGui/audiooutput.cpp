#include "audiooutput.h"
#include <QDebug>
#include <QBuffer>

AudioOutput::AudioOutput(QObject *parent):
    QObject(parent)
{
    QAudioDevice outputDevice;

    for (const auto &device : QMediaDevices::audioOutputs()) {
        outputDevice = device;
        qDebug() << "Falling back to default audio device:" << device.description();
        break;
    }

    if (outputDevice.isNull()) {
        qDebug() << "No valid audio output device found.";
        return;
    }

    m_format.setSampleFormat(QAudioFormat::Int16);
    m_format.setSampleRate(SAMPLE_RATE);
    m_format.setChannelCount(CHANNEL_COUNT);

    if (!outputDevice.isFormatSupported(m_format)) {
        qDebug() << "Desired audio format is not supported by the device. Trying to use nearest format.";
        m_format = outputDevice.preferredFormat();
    }

    m_audioOutput.reset(new QAudioSink(outputDevice, m_format));
    m_audioOutput->setBufferSize(1024 * 1024);
    audioDevice = m_audioOutput->start();

    mutex = new QMutex;

    qDebug() << "Selected audio device:" << outputDevice.description();
    qDebug() << "Audio format: Sample rate:" << m_format.sampleRate()
             << "Channels:" << m_format.channelCount()
             << "Sample format:" << m_format.sampleFormat();
}

AudioOutput::~AudioOutput()
{
    if (audioDevice) {
        audioDevice->close();
    }
    delete mutex;
}

void AudioOutput::handleAudioOutputStateChanged(QAudio::State newState)
{
    if (newState == QAudio::StoppedState) {
    } else if (newState == QAudio::ActiveState) {
    }
}

void AudioOutput::processAudio(const std::vector<float> &audioData)
{
    if (!audioDevice) {
        return;
    }

    buffer.resize(audioData.size() * sizeof(qint16));
    qint16* output = reinterpret_cast<qint16*>(buffer.data());

    for (size_t i = 0; i < audioData.size(); ++i) {
        output[i] = static_cast<qint16>(audioData[i] * 32767.0f);
    }

    QMutexLocker locker(mutex);
    if (m_audioOutput->bytesFree() >= buffer.size()) {
        audioDevice->write(buffer);
    }
}

void AudioOutput::stop()
{
    m_abort = true;
}

void AudioOutput::writeBuffer(const QByteArray &buffer)
{
    if (!m_abort && audioDevice && audioDevice->isOpen())
    {
        QMutexLocker locker(mutex);
        audioDevice->write(buffer);
    }
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
