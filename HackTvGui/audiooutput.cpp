#include "audiooutput.h"
#include <QMediaDevices>
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

        // Check if audio devices are available before creating QAudioSink
        QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
        if (defaultDevice.isNull()) {
            qDebug() << "No default audio output device available";
            return;
        }

        m_format = defaultDevice.preferredFormat();
        m_inputFormat = m_format;

        // Check if the format is supported
        if (!defaultDevice.isFormatSupported(m_format)) {
            qDebug() << "Format not supported!";
            return;
        }

        // Create QAudioSink with explicit device
        m_audioOutput.reset(new QAudioSink(defaultDevice, m_format));

        if (m_audioOutput) {
            // Set buffer size before starting
            m_audioOutput->setBufferSize(1024 * 1024);

            // Connect state change signal for debugging
            connect(m_audioOutput.get(), &QAudioSink::stateChanged,
                    this, &AudioOutput::handleAudioOutputStateChanged);

            // Start the audio output
            audioDevice = m_audioOutput->start();

            if (audioDevice && audioDevice->isOpen()) {
                qDebug() << "Audio initialized successfully";
                qDebug() << "  Device:" << defaultDevice.description();
                qDebug() << "  Buffer size:" << m_audioOutput->bufferSize();
            } else {
                qDebug() << "Failed to start audio device";
                if (m_audioOutput->error() != QAudio::NoError) {
                    qDebug() << "Audio error:" << m_audioOutput->error();
                }
            }
        } else {
            qDebug() << "Failed to create QAudioSink";
        }

    } catch (const std::exception& e) {
        qDebug() << "Exception in AudioOutput constructor:" << e.what();
    } catch (...) {
        qDebug() << "Unknown exception in AudioOutput constructor";
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
    if (!audioDevice || !mutex || !m_audioOutput || audioData.empty()) {
        return;
    }

    try {
        QMutexLocker locker(mutex);

        // Check what format the device expects
        if (m_format.sampleFormat() == QAudioFormat::Float && m_format.channelCount() == 2) {
            // Convert mono float to stereo float
            std::vector<float> stereoData(audioData.size() * 2);
            for (size_t i = 0; i < audioData.size(); ++i) {
                // Clamp input to valid range [-1.0, 1.0]
                float clampedSample = std::max(-1.0f, std::min(1.0f, audioData[i]));
                stereoData[i * 2] = clampedSample;      // Left channel
                stereoData[i * 2 + 1] = clampedSample;  // Right channel (duplicate)
            }

            // Write directly as float
            qint64 bytesToWrite = stereoData.size() * sizeof(float);
            if (m_audioOutput->bytesFree() >= bytesToWrite) {
                audioDevice->write(reinterpret_cast<const char*>(stereoData.data()), bytesToWrite);
            }
        }
        else if (m_format.sampleFormat() == QAudioFormat::Int16 && m_format.channelCount() == 2) {
            // Convert mono float to stereo int16
            std::vector<qint16> stereoData(audioData.size() * 2);
            for (size_t i = 0; i < audioData.size(); ++i) {
                // Clamp input to valid range [-1.0, 1.0] then convert to int16
                float clampedSample = std::max(-1.0f, std::min(1.0f, audioData[i]));
                qint16 sample = static_cast<qint16>(clampedSample * 32767.0f);
                stereoData[i * 2] = sample;      // Left channel
                stereoData[i * 2 + 1] = sample;  // Right channel (duplicate)
            }

            qint64 bytesToWrite = stereoData.size() * sizeof(qint16);
            if (m_audioOutput->bytesFree() >= bytesToWrite) {
                audioDevice->write(reinterpret_cast<const char*>(stereoData.data()), bytesToWrite);
            }
        }
        else if (m_format.sampleFormat() == QAudioFormat::Int16 && m_format.channelCount() == 1) {
            // Convert float to int16 (mono)
            buffer.resize(audioData.size() * sizeof(qint16));
            qint16* output = reinterpret_cast<qint16*>(buffer.data());
            for (size_t i = 0; i < audioData.size(); ++i) {
                // Clamp input to valid range [-1.0, 1.0] then convert to int16
                float clampedSample = std::max(-1.0f, std::min(1.0f, audioData[i]));
                output[i] = static_cast<qint16>(clampedSample * 32767.0f);
            }

            if (m_audioOutput->bytesFree() >= buffer.size()) {
                audioDevice->write(buffer);
            }
        }
        else {
            qDebug() << "Unsupported audio format combination for conversion:"
                     << "Format:" << m_format.sampleFormat()
                     << "Channels:" << m_format.channelCount();
        }
    } catch (const std::exception& e) {
        qDebug() << "Exception in processAudio:" << e.what();
    } catch (...) {
        qDebug() << "Unknown exception in processAudio";
    }
}

void AudioOutput::writeBuffer(const QByteArray &buffer)
{
    if (!m_abort && audioDevice && audioDevice->isOpen() && mutex && !buffer.isEmpty())
    {
        try {
            QMutexLocker locker(mutex);

            // If this buffer contains mono int16 data but device expects stereo float
            if (m_format.sampleFormat() == QAudioFormat::Float && m_format.channelCount() == 2 &&
                m_inputFormat.sampleFormat() == QAudioFormat::Int16 && m_inputFormat.channelCount() == 1) {

                // Validate buffer size
                if (buffer.size() % sizeof(qint16) != 0) {
                    qDebug() << "Invalid buffer size for int16 samples:" << buffer.size();
                    return;
                }

                // Convert mono int16 to stereo float
                const qint16* inputSamples = reinterpret_cast<const qint16*>(buffer.constData());
                int numSamples = buffer.size() / sizeof(qint16);

                std::vector<float> stereoFloat(numSamples * 2);
                for (int i = 0; i < numSamples; ++i) {
                    float sample = inputSamples[i] / 32767.0f;  // Fixed: was 32768
                    stereoFloat[i * 2] = sample;      // Left channel
                    stereoFloat[i * 2 + 1] = sample;  // Right channel (duplicate mono)
                }

                qint64 bytesToWrite = stereoFloat.size() * sizeof(float);
                if (m_audioOutput->bytesFree() >= bytesToWrite) {
                    audioDevice->write(reinterpret_cast<const char*>(stereoFloat.data()), bytesToWrite);
                } else {
                    qDebug() << "Not enough free space in audio buffer:"
                             << "Need:" << bytesToWrite << "Free:" << m_audioOutput->bytesFree();
                }
            }
            else {
                // Direct write if formats match
                if (m_audioOutput->bytesFree() >= buffer.size()) {
                    audioDevice->write(buffer);
                } else {
                    qDebug() << "Not enough free space in audio buffer for direct write:"
                             << "Need:" << buffer.size() << "Free:" << m_audioOutput->bytesFree();
                }
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
