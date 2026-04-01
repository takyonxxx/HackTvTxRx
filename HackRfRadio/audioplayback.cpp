#include "audioplayback.h"
#include <QDebug>
#include <QTimer>
#include <QMediaDevices>
#include <QAudioDevice>
#include <cstring>
#include <cmath>

AudioPlayback::AudioPlayback(QObject *parent)
    : QObject(parent)
{
    m_ring.resize(RING_SIZE, 0.0f);

    m_format.setSampleRate(48000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
}

AudioPlayback::~AudioPlayback()
{
    stop();
}

bool AudioPlayback::start()
{
    if (m_running.load()) return true;

    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    if (outputDevice.isNull()) {
        qDebug() << "No audio output device found";
        return false;
    }

    qDebug() << "Using audio output:" << outputDevice.description();

    m_audioSink = new QAudioSink(outputDevice, m_format, this);
    m_audioSink->setBufferSize(9600); // 100ms buffer

    m_ioDevice = m_audioSink->start();
    if (!m_ioDevice) {
        qDebug() << "Failed to start audio playback";
        delete m_audioSink;
        m_audioSink = nullptr;
        return false;
    }

    m_running.store(true);

    // Timer to write audio data periodically
    m_writeTimer = new QTimer(this);
    connect(m_writeTimer, &QTimer::timeout, this, &AudioPlayback::processAudioQueue);
    m_writeTimer->start(20); // 20ms intervals

    qDebug() << "Audio playback started at 48000 Hz";
    return true;
}

void AudioPlayback::stop()
{
    m_running.store(false);

    if (m_writeTimer) {
        m_writeTimer->stop();
        delete m_writeTimer;
        m_writeTimer = nullptr;
    }

    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
        m_ioDevice = nullptr;
    }

    qDebug() << "Audio playback stopped";
}

void AudioPlayback::enqueueAudio(const std::vector<float>& samples)
{
    if (!m_running.load() || samples.empty()) return;

    size_t w = m_writePos.load(std::memory_order_relaxed);
    for (size_t i = 0; i < samples.size(); i++) {
        m_ring[w] = samples[i];
        w = (w + 1) % RING_SIZE;
    }
    m_writePos.store(w, std::memory_order_release);
}

void AudioPlayback::setVolume(float vol)
{
    m_volume.store(std::clamp(vol, 0.0f, 1.0f));
}

void AudioPlayback::processAudioQueue()
{
    if (!m_ioDevice || !m_running.load()) return;

    size_t w = m_writePos.load(std::memory_order_acquire);
    size_t r = m_readPos.load(std::memory_order_relaxed);
    size_t available = (w >= r) ? (w - r) : (RING_SIZE - r + w);

    if (available == 0) return;

    // Write up to 960 samples (20ms at 48kHz)
    size_t toWrite = std::min(available, size_t(960));
    float vol = m_volume.load();

    // Convert float to int16
    QByteArray outputData(toWrite * sizeof(int16_t), 0);
    int16_t* out = reinterpret_cast<int16_t*>(outputData.data());

    for (size_t i = 0; i < toWrite; i++) {
        float sample = m_ring[r] * vol;
        sample = std::clamp(sample, -1.0f, 1.0f);
        out[i] = static_cast<int16_t>(sample * 32767.0f);
        r = (r + 1) % RING_SIZE;
    }
    m_readPos.store(r, std::memory_order_release);

    m_ioDevice->write(outputData);
}
