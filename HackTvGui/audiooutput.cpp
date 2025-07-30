#include "audiooutput.h"
#include <QDebug>
#include <QBuffer>

AudioOutput::AudioOutput(QObject *parent):
    QObject(parent),
    mutex(nullptr),
    m_abort(false),
    audioDevice(nullptr)
{
    try {
        mutex = new QMutex;

        // Format ayarla
        m_format.setSampleFormat(QAudioFormat::Int16);
        m_format.setSampleRate(SAMPLE_RATE);
        m_format.setChannelCount(CHANNEL_COUNT);
        m_inputFormat = m_format;

        m_audioOutput.reset(new QAudioSink(m_format));  // Device belirtme

        if (m_audioOutput) {
            m_audioOutput->setBufferSize(1024 * 1024);
            audioDevice = m_audioOutput->start();

            if (audioDevice) {
                qDebug() << "Audio initialized successfully with default device";
            } else {
                qDebug() << "Failed to start audio device";
            }
        }

    } catch (const std::exception& e) {
        qDebug() << "Exception in AudioOutput constructor:" << e.what();
    }
}

AudioOutput::~AudioOutput()
{
    try {
        if (audioDevice) {
            audioDevice->close();
            audioDevice = nullptr;
        }

        if (m_audioOutput) {
            m_audioOutput->stop();
            m_audioOutput.reset();
        }

        if (mutex) {
            delete mutex;
            mutex = nullptr;
        }
    } catch (const std::exception& e) {
        qDebug() << "Exception in AudioOutput destructor:" << e.what();
    } catch (...) {
        qDebug() << "Unknown exception in AudioOutput destructor";
    }
}

bool AudioOutput::initializeAudio()
{

}

void AudioOutput::handleAudioOutputStateChanged(QAudio::State newState)
{
    if (newState == QAudio::StoppedState) {
    } else if (newState == QAudio::ActiveState) {
    }
}

void AudioOutput::processAudio(const std::vector<float> &audioData)
{
    if (!audioDevice || !mutex || !m_audioOutput) {
        return;
    }

    try {
        QMutexLocker locker(mutex);

        // Check what format the device expects
        if (m_format.sampleFormat() == QAudioFormat::Float && m_format.channelCount() == 2) {
            // Convert mono float to stereo float
            std::vector<float> stereoData(audioData.size() * 2);
            for (size_t i = 0; i < audioData.size(); ++i) {
                stereoData[i * 2] = audioData[i];      // Left channel
                stereoData[i * 2 + 1] = audioData[i];  // Right channel (duplicate)
            }

            // Write directly as float
            if (m_audioOutput->bytesFree() >= stereoData.size() * sizeof(float)) {
                audioDevice->write(reinterpret_cast<const char*>(stereoData.data()),
                                   stereoData.size() * sizeof(float));
            }
        }
        else if (m_format.sampleFormat() == QAudioFormat::Int16 && m_format.channelCount() == 2) {
            // Convert mono float to stereo int16
            std::vector<qint16> stereoData(audioData.size() * 2);
            for (size_t i = 0; i < audioData.size(); ++i) {
                qint16 sample = static_cast<qint16>(audioData[i] * 32767.0f);
                stereoData[i * 2] = sample;      // Left channel
                stereoData[i * 2 + 1] = sample;  // Right channel (duplicate)
            }

            if (m_audioOutput->bytesFree() >= stereoData.size() * sizeof(qint16)) {
                audioDevice->write(reinterpret_cast<const char*>(stereoData.data()),
                                   stereoData.size() * sizeof(qint16));
            }
        }
        else if (m_format.sampleFormat() == QAudioFormat::Int16 && m_format.channelCount() == 1) {
            // Convert float to int16 (mono)
            buffer.resize(audioData.size() * sizeof(qint16));
            qint16* output = reinterpret_cast<qint16*>(buffer.data());
            for (size_t i = 0; i < audioData.size(); ++i) {
                output[i] = static_cast<qint16>(audioData[i] * 32767.0f);
            }

            if (m_audioOutput->bytesFree() >= buffer.size()) {
                audioDevice->write(buffer);
            }
        }
        else {
            qDebug() << "Unsupported audio format combination for conversion";
        }
    } catch (const std::exception& e) {
        qDebug() << "Exception in processAudio:" << e.what();
    } catch (...) {
        qDebug() << "Unknown exception in processAudio";
    }
}

void AudioOutput::stop()
{
    m_abort = true;
}

void AudioOutput::writeBuffer(const QByteArray &buffer)
{
    if (!m_abort && audioDevice && audioDevice->isOpen() && mutex)
    {
        try {
            QMutexLocker locker(mutex);

            // If this buffer contains mono int16 data but device expects stereo float
            if (m_format.sampleFormat() == QAudioFormat::Float && m_format.channelCount() == 2 &&
                m_inputFormat.sampleFormat() == QAudioFormat::Int16 && m_inputFormat.channelCount() == 1) {

                // Convert mono int16 to stereo float
                const qint16* inputSamples = reinterpret_cast<const qint16*>(buffer.constData());
                int numSamples = buffer.size() / sizeof(qint16);

                std::vector<float> stereoFloat(numSamples * 2);
                for (int i = 0; i < numSamples; ++i) {
                    float sample = inputSamples[i] / 32768.0f;  // Convert to float [-1, 1]
                    stereoFloat[i * 2] = sample;      // Left channel
                    stereoFloat[i * 2 + 1] = sample;  // Right channel (duplicate mono)
                }

                audioDevice->write(reinterpret_cast<const char*>(stereoFloat.data()),
                                   stereoFloat.size() * sizeof(float));
            }
            else {
                // Direct write if formats match
                audioDevice->write(buffer);
            }
        } catch (const std::exception& e) {
            qDebug() << "Exception in writeBuffer:" << e.what();
        } catch (...) {
            qDebug() << "Unknown exception in writeBuffer";
        }
    }
}

void AudioOutput::setVolume(int value)
{
    if (m_audioOutput) {
        try {
            qreal linearVolume = value / 100.0;
            qreal volume = QAudio::convertVolume(linearVolume,
                                                 QAudio::LinearVolumeScale,
                                                 QAudio::LogarithmicVolumeScale);
            m_audioOutput->setVolume(volume);
        } catch (const std::exception& e) {
            qDebug() << "Exception in setVolume:" << e.what();
        } catch (...) {
            qDebug() << "Unknown exception in setVolume";
        }
    }
}
