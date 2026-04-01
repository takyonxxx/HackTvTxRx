#include "audiocapture.h"
#include <QDebug>

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
{
    m_format.setSampleRate(44100);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Float);
    m_accumulator.reserve(4096);
}

AudioCapture::~AudioCapture()
{
    stop();
}

bool AudioCapture::start()
{
    if (m_running.load()) return true;

    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        qDebug() << "No audio input device found";
        return false;
    }

    qDebug() << "Using audio input:" << inputDevice.description();

    if (!inputDevice.isFormatSupported(m_format)) {
        m_format = inputDevice.preferredFormat();
        m_format.setChannelCount(1);
        m_format.setSampleFormat(QAudioFormat::Float);
    }

    { std::lock_guard<std::mutex> lock(m_accMutex); m_accumulator.clear(); }

    m_audioSource = new QAudioSource(inputDevice, m_format, this);
    m_audioSource->setBufferSize(4096);

    m_ioDevice = m_audioSource->start();
    if (!m_ioDevice) {
        qDebug() << "Failed to start audio capture";
        delete m_audioSource; m_audioSource = nullptr;
        return false;
    }

    connect(m_ioDevice, &QIODevice::readyRead, this, &AudioCapture::onReadyRead);

    m_flushTimer = new QTimer(this);
    connect(m_flushTimer, &QTimer::timeout, this, &AudioCapture::onFlushTimer);
    m_flushTimer->start(10);

    m_running.store(true);
    qDebug() << "Audio capture started at" << m_format.sampleRate() << "Hz, flush=10ms";
    return true;
}

void AudioCapture::stop()
{
    m_running.store(false);
    if (m_flushTimer) { m_flushTimer->stop(); delete m_flushTimer; m_flushTimer = nullptr; }
    if (m_audioSource) { m_audioSource->stop(); delete m_audioSource; m_audioSource = nullptr; m_ioDevice = nullptr; }
    { std::lock_guard<std::mutex> lock(m_accMutex); m_accumulator.clear(); }
    qDebug() << "Audio capture stopped";
}

void AudioCapture::onReadyRead()
{
    if (!m_ioDevice || !m_running.load()) return;

    QByteArray data = m_ioDevice->readAll();
    if (data.isEmpty()) return;

    const float* p = reinterpret_cast<const float*>(data.constData());
    size_t n = data.size() / sizeof(float);

    if (n > 0) {
        std::lock_guard<std::mutex> lock(m_accMutex);
        m_accumulator.insert(m_accumulator.end(), p, p + n);
        if (m_accumulator.size() >= SEND_THRESHOLD) {
            emit audioDataReady(m_accumulator);
            m_accumulator.clear();
        }
    }
}

void AudioCapture::onFlushTimer()
{
    if (!m_running.load()) return;
    std::lock_guard<std::mutex> lock(m_accMutex);
    if (!m_accumulator.empty()) {
        emit audioDataReady(m_accumulator);
        m_accumulator.clear();
    }
}
