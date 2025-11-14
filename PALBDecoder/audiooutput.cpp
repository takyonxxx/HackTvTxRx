#include "audiooutput.h"
#include <QMediaDevices>
#include <QDebug>
#include <algorithm>

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
{
    // Pre-allocate circular buffer
    audioBuffer.resize(RESERVE_SIZE);
    outputBuffer.resize(CHUNK_SIZE * 2 * sizeof(qint16));

    initializeAudio();

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

        // Larger buffer for stability
        m_audioOutput->setBufferSize(4 * 1024 * 1024);  // 4 MB

        connect(m_audioOutput.get(), &QAudioSink::stateChanged,
                this, &AudioOutput::handleAudioOutputStateChanged);

        audioDevice = m_audioOutput->start();

        if (!audioDevice || !audioDevice->isOpen()) {
            qCritical() << "Failed to start audio device";
            return false;
        }

        qDebug() << "Audio initialized:";
        qDebug() << "  Format: 48kHz, 2ch, Int16";
        qDebug() << "  Buffer:" << m_audioOutput->bufferSize() / 1024 << "KB";
        qDebug() << "  Chunk size:" << CHUNK_SIZE << "samples ("
                 << (CHUNK_SIZE * 1000 / SAMPLE_RATE) << "ms)";
        qDebug() << "  Priming:" << MIN_BUFFER_SAMPLES << "samples ("
                 << (MIN_BUFFER_SAMPLES * 1000 / SAMPLE_RATE) << "ms)";

        return true;

    } catch (const std::exception& e) {
        qCritical() << "Exception in audio initialization:" << e.what();
        return false;
    }
}

void AudioOutput::handleAudioOutputStateChanged(QAudio::State newState)
{
    switch (newState) {
    case QAudio::IdleState:
        qDebug() << "Audio: Idle";
        break;
    case QAudio::ActiveState:
        // Normal operation
        break;
    case QAudio::SuspendedState:
        qWarning() << "Audio: Suspended";
        break;
    case QAudio::StoppedState:
        if (m_audioOutput && m_audioOutput->error() != QAudio::NoError) {
            qCritical() << "Audio error:" << m_audioOutput->error();
        }
        break;
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

    // Check overflow - drop oldest samples if buffer full
    if (bufferSize + samplesToAdd > MAX_QUEUE_SIZE) {
        size_t overflow = (bufferSize + samplesToAdd) - MAX_QUEUE_SIZE;
        readPos = (readPos + overflow) % RESERVE_SIZE;
        bufferSize -= overflow;

        static int dropCount = 0;
        if (++dropCount % 100 == 0) {
            qWarning() << "Audio buffer overflow! Dropped" << overflow << "samples";
        }
    }

    // Fast batch copy - handle wraparound
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

    // Wake up writer thread
    if (bufferSize >= MIN_BUFFER_SAMPLES) {
        queueNotEmpty.wakeOne();
    }
}

void AudioOutput::audioWriterLoop()
{
    bool bufferPrimed = false;
    std::vector<float> chunk;
    chunk.reserve(CHUNK_SIZE);

    int consecutiveUnderruns = 0;

    while (m_running) {
        chunk.clear();

        {
            QMutexLocker locker(&mutex);

            // Initial buffer priming - wait for sufficient data
            if (!bufferPrimed) {
                if (bufferSize < MIN_BUFFER_SAMPLES) {
                    queueNotEmpty.wait(&mutex, 2000);  // 2000ms wait - daha uzun bekle
                    continue;
                }
                bufferPrimed = true;
                consecutiveUnderruns = 0;
                qDebug() << "Audio buffer primed with" << bufferSize << "samples ("
                         << (bufferSize * 1000.0 / SAMPLE_RATE) << "ms)";
            }

            // Check for underrun
            if (bufferSize < CHUNK_SIZE) {
                consecutiveUnderruns++;

                // If too many consecutive underruns, re-prime the buffer
                if (consecutiveUnderruns > 5) {  // 10→5
                    bufferPrimed = false;
                    qWarning() << "Too many underruns, re-priming buffer...";
                    queueNotEmpty.wait(&mutex, 1000);  // 500ms→1000ms
                    continue;
                }

                queueNotEmpty.wait(&mutex, 100);
                continue;
            }

            // Reset underrun counter on successful read
            if (consecutiveUnderruns > 0) {
                consecutiveUnderruns = 0;
            }

            // Fast batch read from circular buffer
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

    qDebug() << "Audio writer thread stopped";
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

    // Fast conversion: mono to stereo with clipping
    for (size_t i = 0; i < audioData.size(); ++i) {
        // Clamp and convert to 16-bit
        float sample = std::clamp(audioData[i], -1.0f, 1.0f);
        qint16 intSample = static_cast<qint16>(sample * 32767.0f);

        output[i * 2] = intSample;      // Left
        output[i * 2 + 1] = intSample;  // Right
    }

    qint64 bytesToWrite = audioData.size() * 2 * sizeof(qint16);
    qint64 bytesWritten = 0;

    // Write to audio device
    while (bytesWritten < bytesToWrite && m_running) {
        // Wait for space in output buffer
        qint64 freeBytes = m_audioOutput->bytesFree();
        if (freeBytes < (bytesToWrite - bytesWritten)) {
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
