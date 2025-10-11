#include "audiooutput.h"
#include <QMediaDevices>
#include <QDebug>
#include <algorithm>

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
{
    // Pre-allocate circular buffer
    audioBuffer.resize(RESERVE_SIZE);
    outputBuffer.resize(CHUNK_SIZE * 2 * sizeof(qint16));  // Pre-allocate output buffer

    initializeAudio();

    moveToThread(&audioWriterThread);
    connect(&audioWriterThread, &QThread::started, this, &AudioOutput::audioWriterLoop);
    audioWriterThread.start();

    qDebug() << "AudioOutput initialized with circular buffer, capacity:" << RESERVE_SIZE;
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
            qCritical() << "No default audio output device available";
            return false;
        }

        m_format = defaultDevice.preferredFormat();
        m_format.setSampleRate(SAMPLE_RATE);
        m_format.setChannelCount(CHANNEL_COUNT);
        m_format.setSampleFormat(QAudioFormat::Int16);

        m_inputFormat = m_format;

        if (!defaultDevice.isFormatSupported(m_format)) {
            qCritical() << "Audio format not supported!";
            return false;
        }

        m_audioOutput.reset(new QAudioSink(defaultDevice, m_format));
        if (!m_audioOutput) return false;

        m_audioOutput->setBufferSize(2 * 1024 * 1024);

        connect(m_audioOutput.get(), &QAudioSink::stateChanged,
                this, &AudioOutput::handleAudioOutputStateChanged);

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
    if (newState == QAudio::StoppedState) {
        if (m_audioOutput && m_audioOutput->error() != QAudio::NoError) {
            qCritical() << "Audio error:" << m_audioOutput->error();
        }
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

void AudioOutput::enqueueAudio(const std::vector<float>& samples)
{
    if (samples.empty()) return;

    QMutexLocker locker(&mutex);

    size_t samplesToAdd = samples.size();

    // Check overflow
    if (bufferSize + samplesToAdd > MAX_QUEUE_SIZE) {
        // Remove oldest samples (advance read position)
        size_t overflow = (bufferSize + samplesToAdd) - MAX_QUEUE_SIZE;
        readPos = (readPos + overflow) % RESERVE_SIZE;
        bufferSize -= overflow;
    }

    // FAST BATCH COPY - split into two memcpy if wrapping
    size_t firstChunk = std::min(samplesToAdd, RESERVE_SIZE - writePos);
    std::copy(samples.begin(), samples.begin() + firstChunk,
              audioBuffer.begin() + writePos);

    if (samplesToAdd > firstChunk) {
        // Wrap around
        size_t secondChunk = samplesToAdd - firstChunk;
        std::copy(samples.begin() + firstChunk, samples.end(),
                  audioBuffer.begin());
        writePos = secondChunk;
    } else {
        writePos = (writePos + firstChunk) % RESERVE_SIZE;
    }

    bufferSize += samplesToAdd;

    queueNotEmpty.wakeOne();
}

void AudioOutput::audioWriterLoop()
{
    bool bufferPrimed = false;
    std::vector<float> chunk;
    chunk.reserve(CHUNK_SIZE);

    while (m_running) {
        chunk.clear();

        {
            QMutexLocker locker(&mutex);

            // Buffer priming
            if (!bufferPrimed) {
                if (bufferSize < MIN_BUFFER_SAMPLES) {
                    queueNotEmpty.wait(&mutex, 100);
                    continue;
                }
                bufferPrimed = true;
            }

            // Underrun check
            if (bufferSize < CHUNK_SIZE) {
                bufferPrimed = false;
                queueNotEmpty.wait(&mutex, 50);
                continue;
            }

            // FAST BATCH READ - circular buffer
            size_t firstChunk = std::min<size_t>(CHUNK_SIZE, RESERVE_SIZE - readPos);
            chunk.insert(chunk.end(),
                         audioBuffer.begin() + readPos,
                         audioBuffer.begin() + readPos + firstChunk);

            if (CHUNK_SIZE > firstChunk) {
                // Wrap around
                size_t secondChunk = CHUNK_SIZE - firstChunk;
                chunk.insert(chunk.end(),
                             audioBuffer.begin(),
                             audioBuffer.begin() + secondChunk);
                readPos = secondChunk;
            } else {
                readPos = (readPos + firstChunk) % RESERVE_SIZE;
            }

            bufferSize -= CHUNK_SIZE;
        }

        if (!chunk.empty()) {
            processAudio(chunk);
        }
    }
}

void AudioOutput::processAudio(const std::vector<float>& audioData)
{
    if (!audioDevice || audioData.empty()) return;

    // Use pre-allocated buffer
    size_t requiredSize = audioData.size() * 2 * sizeof(qint16);
    if (outputBuffer.size() < requiredSize) {
        outputBuffer.resize(requiredSize);
    }

    qint16* output = reinterpret_cast<qint16*>(outputBuffer.data());

    // FAST CONVERSION - mono to stereo
    for (size_t i = 0; i < audioData.size(); ++i) {
        qint16 sample = static_cast<qint16>(
            std::clamp(audioData[i], -1.0f, 1.0f) * 32767.0f
            );
        output[i * 2] = sample;      // Left
        output[i * 2 + 1] = sample;  // Right
    }

    qint64 bytesToWrite = audioData.size() * 2 * sizeof(qint16);
    qint64 bytesWritten = 0;

    // Blocking write
    while (bytesWritten < bytesToWrite && m_running) {
        if (m_audioOutput->bytesFree() < bytesToWrite) {
            QThread::msleep(5);
            continue;
        }

        qint64 written = audioDevice->write(
            outputBuffer.constData() + bytesWritten,
            bytesToWrite - bytesWritten
            );

        if (written > 0) {
            bytesWritten += written;
        } else if (written < 0) {
            qCritical() << "Audio write error!";
            break;
        } else {
            QThread::msleep(1);
        }
    }
}

int AudioOutput::queueSize() const
{
    QMutexLocker locker(&mutex);
    return static_cast<int>(bufferSize);
}

double AudioOutput::queueDuration() const
{
    QMutexLocker locker(&mutex);
    return bufferSize / static_cast<double>(SAMPLE_RATE);
}

void AudioOutput::stop()
{
    m_running = false;
    queueNotEmpty.wakeAll();
}
