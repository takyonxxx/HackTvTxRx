#include "audioplayback.h"
#include <QDebug>
#include <QMediaDevices>
#include <QAudioDevice>
#include <cstring>
#include <cmath>
#include <algorithm>

AudioPlayback::AudioPlayback(QObject *parent)
    : QObject(parent)
{
    m_ring.resize(RING_SIZE, 0.0f);
    m_outputBuffer.resize(CHUNK_SIZE * 2 * sizeof(qint16));

    // Stereo Int16 at 48kHz - matches HackTvGui AudioOutput
    m_format.setSampleRate(SAMPLE_RATE);
    m_format.setChannelCount(2);
    m_format.setSampleFormat(QAudioFormat::Int16);
}

AudioPlayback::~AudioPlayback()
{
    stop();
}

bool AudioPlayback::initAudioSink()
{
    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    if (outputDevice.isNull()) {
        qDebug() << "No audio output device found";
        return false;
    }

    qDebug() << "Using audio output:" << outputDevice.description();

    m_audioSink = std::make_unique<QAudioSink>(outputDevice, m_format);
    m_audioSink->setBufferSize(2 * 1024 * 1024); // 2MB buffer

    m_ioDevice = m_audioSink->start();
    if (!m_ioDevice || !m_ioDevice->isOpen()) {
        qDebug() << "Failed to start audio sink";
        m_audioSink.reset();
        return false;
    }

    return true;
}

bool AudioPlayback::start()
{
    if (m_running.load()) return true;

    if (!initAudioSink()) return false;

    m_running.store(true);

    // Writer thread - uses QThread::create to avoid moveToThread issues
    m_writerThread.setObjectName("AudioWriter");
    QObject::connect(&m_writerThread, &QThread::started, [this]() {
        writerLoop();
    });
    m_writerThread.start();

    qDebug() << "Audio playback started at" << SAMPLE_RATE
             << "Hz, stereo, buffer:" << m_audioSink->bufferSize();
    return true;
}

void AudioPlayback::stop()
{
    if (!m_running.load()) return;

    m_running.store(false);
    m_queueNotEmpty.wakeAll();

    if (m_writerThread.isRunning()) {
        m_writerThread.quit();
        m_writerThread.wait(2000);
        if (m_writerThread.isRunning()) {
            m_writerThread.terminate();
            m_writerThread.wait();
        }
    }

    if (m_ioDevice) {
        m_ioDevice = nullptr;
    }

    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink.reset();
    }

    // Reset ring buffer
    m_writePos = 0;
    m_readPos = 0;
    m_bufferSize = 0;

    qDebug() << "Audio playback stopped";
}

void AudioPlayback::enqueueAudio(const std::vector<float>& samples)
{
    if (!m_running.load() || samples.empty()) return;

    QMutexLocker locker(&m_mutex);

    size_t samplesToAdd = samples.size();

    // Overflow: drop oldest data
    if (m_bufferSize + samplesToAdd > MAX_QUEUE_SIZE) {
        size_t overflow = (m_bufferSize + samplesToAdd) - MAX_QUEUE_SIZE;
        m_readPos = (m_readPos + overflow) % RING_SIZE;
        m_bufferSize -= overflow;
    }

    // Batch copy into ring (handle wrap)
    size_t firstChunk = std::min(samplesToAdd, RING_SIZE - m_writePos);
    std::copy(samples.begin(), samples.begin() + firstChunk,
              m_ring.begin() + m_writePos);

    if (samplesToAdd > firstChunk) {
        size_t secondChunk = samplesToAdd - firstChunk;
        std::copy(samples.begin() + firstChunk, samples.end(),
                  m_ring.begin());
        m_writePos = secondChunk;
    } else {
        m_writePos = (m_writePos + firstChunk) % RING_SIZE;
    }

    m_bufferSize += samplesToAdd;
    m_queueNotEmpty.wakeOne();
}

void AudioPlayback::setVolume(float vol)
{
    m_volume.store(std::clamp(vol, 0.0f, 1.0f));
}

void AudioPlayback::writerLoop()
{
    bool bufferPrimed = false;
    std::vector<float> chunk;
    chunk.reserve(CHUNK_SIZE);

    while (m_running.load()) {
        chunk.clear();

        {
            QMutexLocker locker(&m_mutex);

            // Buffer priming - wait for 100ms of data before starting
            if (!bufferPrimed) {
                if (m_bufferSize < MIN_BUFFER_SAMPLES) {
                    m_queueNotEmpty.wait(&m_mutex, 100);
                    continue;
                }
                bufferPrimed = true;
            }

            // Underrun - re-prime
            if (m_bufferSize < CHUNK_SIZE) {
                bufferPrimed = false;
                m_queueNotEmpty.wait(&m_mutex, 50);
                continue;
            }

            // Batch read from ring
            size_t firstChunk = std::min<size_t>(CHUNK_SIZE, RING_SIZE - m_readPos);
            chunk.insert(chunk.end(),
                         m_ring.begin() + m_readPos,
                         m_ring.begin() + m_readPos + firstChunk);

            if (CHUNK_SIZE > firstChunk) {
                size_t secondChunk = CHUNK_SIZE - firstChunk;
                chunk.insert(chunk.end(),
                             m_ring.begin(),
                             m_ring.begin() + secondChunk);
                m_readPos = secondChunk;
            } else {
                m_readPos = (m_readPos + firstChunk) % RING_SIZE;
            }

            m_bufferSize -= CHUNK_SIZE;
        }

        if (!chunk.empty()) {
            writeChunk(chunk);
        }
    }
}

void AudioPlayback::writeChunk(const std::vector<float>& audioData)
{
    if (!m_ioDevice || !m_audioSink || audioData.empty()) return;

    float vol = m_volume.load();

    // Mono -> Stereo conversion
    size_t requiredSize = audioData.size() * 2 * sizeof(qint16);
    if (static_cast<size_t>(m_outputBuffer.size()) < requiredSize) {
        m_outputBuffer.resize(requiredSize);
    }

    qint16* output = reinterpret_cast<qint16*>(m_outputBuffer.data());

    for (size_t i = 0; i < audioData.size(); ++i) {
        float sample = std::clamp(audioData[i] * vol, -1.0f, 1.0f);
        qint16 s16 = static_cast<qint16>(sample * 32767.0f);
        output[i * 2]     = s16; // Left
        output[i * 2 + 1] = s16; // Right
    }

    qint64 bytesToWrite = static_cast<qint64>(requiredSize);
    qint64 bytesWritten = 0;

    // Blocking write - wait for audio sink space
    while (bytesWritten < bytesToWrite && m_running.load()) {
        qint64 remaining = bytesToWrite - bytesWritten;
        if (m_audioSink->bytesFree() < remaining) {
            QThread::msleep(5);
            continue;
        }

        qint64 written = m_ioDevice->write(
            m_outputBuffer.constData() + bytesWritten, remaining);

        if (written > 0) {
            bytesWritten += written;
        } else if (written < 0) {
            qCritical() << "Audio write error";
            break;
        } else {
            QThread::msleep(1);
        }
    }
}
