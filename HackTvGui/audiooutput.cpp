#include "audiooutput.h"
#include <QDebug>
#include <QBuffer>

AudioOutput::AudioOutput(QObject *parent, int sampleRate):
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

    m_format.setSampleFormat(QAudioFormat::Float);
    m_format.setSampleRate(sampleRate);
    m_format.setChannelCount(1);

    if (!outputDevice.isFormatSupported(m_format)) {
        qDebug() << "Desired audio format is not supported by the device. Trying to use nearest format.";
        m_format = outputDevice.preferredFormat();
    }

    m_audioOutput.reset(new QAudioSink(outputDevice, m_format));
    m_audioOutput->setBufferSize(1024 * 512);
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
