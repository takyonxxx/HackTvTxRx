#include "audiooutput.h"
#include <QMediaDevices>
#include <QDebug>
#include <algorithm>

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
    , audioDevice(nullptr)
    , audioWriterThread(nullptr)
{
    // Pre-allocate conversion buffer
    conversionBuffer.resize(CHUNK_SIZE * 2);  // Stereo

    // Create and start worker thread
    audioWriterThread = new QThread(this);
    audioWriterThread->start(QThread::HighPriority);

    // Initialize audio on worker thread
    QMetaObject::invokeMethod(this, [this]() {
        initializeAudio();
        audioWriterLoop();  // Start processing loop
    }, Qt::QueuedConnection);

    // Move this object to worker thread
    this->moveToThread(audioWriterThread);

    qDebug() << "AudioOutput created, worker thread starting...";
}

AudioOutput::~AudioOutput()
{
    qDebug() << "AudioOutput destructor started";

    m_running = false;
    queueNotEmpty.wakeAll();

    if (audioWriterThread) {
        audioWriterThread->quit();
        if (!audioWriterThread->wait(2000)) {
            qWarning() << "Audio thread did not stop, terminating...";
            audioWriterThread->terminate();
            audioWriterThread->wait(1000);
        }
    }

    if (audioDevice) {
        audioDevice->close();
        audioDevice = nullptr;
    }

    if (m_audioOutput) {
        m_audioOutput->stop();
        m_audioOutput.reset();
    }

    qDebug() << "AudioOutput destroyed";
}

bool AudioOutput::initializeAudio()
{
    qDebug() << "Initializing audio on thread:" << QThread::currentThread();

    try {
        QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
        if (defaultDevice.isNull()) {
            qCritical() << "No default audio output device available";
            return false;
        }

        m_format.setSampleRate(SAMPLE_RATE);
        m_format.setChannelCount(CHANNEL_COUNT);
        m_format.setSampleFormat(QAudioFormat::Int16);

        if (!defaultDevice.isFormatSupported(m_format)) {
            qCritical() << "Audio format not supported!";
            return false;
        }

        m_audioOutput.reset(new QAudioSink(defaultDevice, m_format));
        if (!m_audioOutput) return false;

        // Large buffer for stable playback
        m_audioOutput->setBufferSize(480000);  // 5 seconds

        connect(m_audioOutput.get(), &QAudioSink::stateChanged,
                this, &AudioOutput::handleAudioOutputStateChanged,
                Qt::DirectConnection);

        audioDevice = m_audioOutput->start();

        if (!audioDevice || !audioDevice->isOpen()) {
            qCritical() << "Failed to start audio device";
            return false;
        }

        qDebug() << "Audio initialized: 48kHz, 2ch, buffer:" << m_audioOutput->bufferSize();
        return true;

    } catch (const std::exception& e) {
        qCritical() << "Exception in audio initialization:" << e.what();
        return false;
    }
}

void AudioOutput::handleAudioOutputStateChanged(QAudio::State newState)
{
    static QAudio::State lastState = QAudio::IdleState;
    if (newState != lastState) {
        qDebug() << "Audio state:" << newState;
        lastState = newState;
    }
}

void AudioOutput::setVolume(int value)
{
    QMetaObject::invokeMethod(this, [this, value]() {
        if (m_audioOutput) {
            qreal linearVolume = value / 100.0;
            qreal volume = QAudio::convertVolume(linearVolume,
                                                 QAudio::LinearVolumeScale,
                                                 QAudio::LogarithmicVolumeScale);
            m_audioOutput->setVolume(volume);
        }
    }, Qt::QueuedConnection);
}

void AudioOutput::enqueueAudio(const std::vector<float>& samples)
{
    if (samples.empty() || !m_running) return;

    QMutexLocker locker(&mutex);

    // Debug first few
    static int count = 0;
    if (count++ < 3) {
        qDebug() << "Enqueue:" << samples.size() << "samples, queue:" << audioQueue.size();
    }

    // Prevent overflow
    while (audioQueue.size() + samples.size() > MAX_QUEUE_SIZE) {
        // Remove oldest samples
        size_t toRemove = std::min<size_t>(CHUNK_SIZE, audioQueue.size());
        audioQueue.erase(audioQueue.begin(), audioQueue.begin() + toRemove);
    }

    // Add samples
    audioQueue.insert(audioQueue.end(), samples.begin(), samples.end());

    queueNotEmpty.wakeOne();
}

void AudioOutput::audioWriterLoop()
{
    qDebug() << "Audio writer loop started on thread:" << QThread::currentThread();

    bool bufferPrimed = false;

    while (m_running) {
        std::vector<float> chunk;

        {
            QMutexLocker locker(&mutex);

            // Initial buffer priming
            if (!bufferPrimed) {
                if (audioQueue.size() < MIN_BUFFER_SAMPLES) {
                    queueNotEmpty.wait(&mutex, 100);
                    continue;
                }
                bufferPrimed = true;
                qDebug() << "Audio buffer primed with" << audioQueue.size() << "samples ("
                         << (audioQueue.size() / 48.0) << "ms)";
            }

            // Check if we have enough data
            if (audioQueue.size() < CHUNK_SIZE) {
                // Underrun warning
                if (bufferPrimed) {
                    qWarning() << "Audio buffer underrun! Queue:" << audioQueue.size();
                    bufferPrimed = false;
                }
                queueNotEmpty.wait(&mutex, 50);
                continue;
            }

            // Extract chunk
            chunk.reserve(CHUNK_SIZE);
            chunk.insert(chunk.end(),
                         audioQueue.begin(),
                         audioQueue.begin() + CHUNK_SIZE);

            audioQueue.erase(audioQueue.begin(), audioQueue.begin() + CHUNK_SIZE);
        }

        // Process outside mutex
        if (!chunk.empty()) {
            processAudioChunk(chunk);
        }
    }

    qDebug() << "Audio writer loop ended";
}

void AudioOutput::processAudioChunk(const std::vector<float>& audioData)
{
    if (!audioDevice || audioData.empty()) return;

    // Convert to stereo int16
    for (size_t i = 0; i < audioData.size(); ++i) {
        qint16 sample = static_cast<qint16>(
            std::clamp(audioData[i], -1.0f, 1.0f) * 32767.0f
            );
        conversionBuffer[i * 2] = sample;      // Left
        conversionBuffer[i * 2 + 1] = sample;  // Right
    }

    const char* data = reinterpret_cast<const char*>(conversionBuffer.data());
    qint64 bytesToWrite = audioData.size() * 2 * sizeof(qint16);
    qint64 totalWritten = 0;

    // Blocking write with timeout
    int attempts = 0;
    while (totalWritten < bytesToWrite && m_running && attempts < 100) {
        qint64 written = audioDevice->write(data + totalWritten, bytesToWrite - totalWritten);

        if (written > 0) {
            totalWritten += written;
        } else if (written < 0) {
            qCritical() << "Audio write error!";
            break;
        } else {
            // No space, wait a bit
            QThread::msleep(5);
            attempts++;
        }
    }

    if (attempts >= 100) {
        qWarning() << "Audio write timeout!";
    }
}

int AudioOutput::queueSize() const
{
    QMutexLocker locker(&mutex);
    return static_cast<int>(audioQueue.size());
}

double AudioOutput::queueDuration() const
{
    QMutexLocker locker(&mutex);
    return audioQueue.size() / static_cast<double>(SAMPLE_RATE);
}

void AudioOutput::stop()
{
    m_running = false;
    queueNotEmpty.wakeAll();
}
