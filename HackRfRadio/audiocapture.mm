#include "audiocapture.h"
#include <QDebug>

#if defined(Q_OS_IOS)
#include <AVFoundation/AVFoundation.h>

static bool setupiOSAudioSession()
{
    AVAudioSession *session = [AVAudioSession sharedInstance];
    NSError *error = nil;

    // Set category to PlayAndRecord to enable mic input alongside speaker output
    [session setCategory:AVAudioSessionCategoryPlayAndRecord
             withOptions:AVAudioSessionCategoryOptionDefaultToSpeaker |
                         AVAudioSessionCategoryOptionAllowBluetooth
                   error:&error];
    if (error) {
        qDebug() << "AVAudioSession setCategory failed:" << error.localizedDescription.UTF8String;
        return false;
    }

    // Set preferred sample rate to 48kHz to match playback
    [session setPreferredSampleRate:48000.0 error:&error];
    if (error) {
        qDebug() << "AVAudioSession setPreferredSampleRate failed:" << error.localizedDescription.UTF8String;
        // Not fatal, continue
    }

    // Set preferred IO buffer duration (smaller = lower latency)
    [session setPreferredIOBufferDuration:0.01 error:&error]; // 10ms

    [session setActive:YES error:&error];
    if (error) {
        qDebug() << "AVAudioSession setActive failed:" << error.localizedDescription.UTF8String;
        return false;
    }

    qDebug() << "AVAudioSession configured: PlayAndRecord, sampleRate:"
             << session.sampleRate << "inputChannels:" << session.inputNumberOfChannels;
    return true;
}

static double getiOSSessionSampleRate()
{
    return [AVAudioSession sharedInstance].sampleRate;
}
#endif

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
{
    m_format.setSampleRate(48000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
    m_accumulator.reserve(4096);
}

AudioCapture::~AudioCapture()
{
    stop();
}

bool AudioCapture::start()
{
    if (m_running.load()) return true;

#if defined(Q_OS_IOS)
    if (!setupiOSAudioSession()) {
        qDebug() << "Failed to setup iOS audio session";
        return false;
    }
#endif

    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        qDebug() << "No audio input device found";
        return false;
    }

    qDebug() << "Using audio input:" << inputDevice.description();

    // On iOS, we must match the AVAudioSession's actual sample rate.
    // The device preferred format may report a different rate than what
    // the session is actually configured to use, causing AudioUnitRender -50.
#if defined(Q_OS_IOS)
    double sessionRate = getiOSSessionSampleRate();
    m_format.setSampleRate(static_cast<int>(sessionRate));
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
    qDebug() << "iOS: using AVAudioSession sampleRate:" << sessionRate;

    // Verify this format is supported, if not try Float32
    if (!inputDevice.isFormatSupported(m_format)) {
        m_format.setSampleFormat(QAudioFormat::Float);
        qDebug() << "iOS: Int16 not supported, trying Float32";
        if (!inputDevice.isFormatSupported(m_format)) {
            // Last resort: use device preferred format entirely
            m_format = inputDevice.preferredFormat();
            qDebug() << "iOS: using device preferred as last resort:" 
                     << m_format.sampleRate() << "Hz" << m_format.channelCount() << "ch";
        }
    }
#else
    m_format = inputDevice.preferredFormat();
#endif
    qDebug() << "Audio capture format:" << m_format.sampleRate() << "Hz"
             << m_format.channelCount() << "ch"
             << m_format.sampleFormat();

    m_inputChannels = m_format.channelCount();
    m_inputSampleFormat = m_format.sampleFormat();

    { std::lock_guard<std::mutex> lock(m_accMutex); m_accumulator.clear(); }

    m_audioSource = new QAudioSource(inputDevice, m_format, this);
    m_audioSource->setBufferSize(8192);

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
    qDebug() << "Audio capture started at" << m_format.sampleRate() << "Hz,"
             << m_format.channelCount() << "ch,"
             << "format:" << m_format.sampleFormat() << "flush=10ms";
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

    // Convert input to float mono samples regardless of input format
    std::vector<float> monoSamples;

    if (m_inputSampleFormat == QAudioFormat::Float) {
        const float* p = reinterpret_cast<const float*>(data.constData());
        size_t totalSamples = data.size() / sizeof(float);
        if (m_inputChannels >= 2) {
            size_t frames = totalSamples / m_inputChannels;
            monoSamples.resize(frames);
            for (size_t i = 0; i < frames; i++)
                monoSamples[i] = p[i * m_inputChannels]; // take left channel
        } else {
            monoSamples.assign(p, p + totalSamples);
        }
    } else if (m_inputSampleFormat == QAudioFormat::Int16) {
        const int16_t* p = reinterpret_cast<const int16_t*>(data.constData());
        size_t totalSamples = data.size() / sizeof(int16_t);
        if (m_inputChannels >= 2) {
            size_t frames = totalSamples / m_inputChannels;
            monoSamples.resize(frames);
            for (size_t i = 0; i < frames; i++)
                monoSamples[i] = p[i * m_inputChannels] / 32768.0f;
        } else {
            monoSamples.resize(totalSamples);
            for (size_t i = 0; i < totalSamples; i++)
                monoSamples[i] = p[i] / 32768.0f;
        }
    } else if (m_inputSampleFormat == QAudioFormat::Int32) {
        const int32_t* p = reinterpret_cast<const int32_t*>(data.constData());
        size_t totalSamples = data.size() / sizeof(int32_t);
        if (m_inputChannels >= 2) {
            size_t frames = totalSamples / m_inputChannels;
            monoSamples.resize(frames);
            for (size_t i = 0; i < frames; i++)
                monoSamples[i] = p[i * m_inputChannels] / 2147483648.0f;
        } else {
            monoSamples.resize(totalSamples);
            for (size_t i = 0; i < totalSamples; i++)
                monoSamples[i] = p[i] / 2147483648.0f;
        }
    } else {
        return; // unsupported format
    }

    if (!monoSamples.empty()) {
        std::lock_guard<std::mutex> lock(m_accMutex);
        m_accumulator.insert(m_accumulator.end(), monoSamples.begin(), monoSamples.end());
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
