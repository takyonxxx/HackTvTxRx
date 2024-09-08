#ifndef AUDIOINPUT_H
#define AUDIOINPUT_H

#include <QCoreApplication>
#include <QObject>
#include <QAudioSource>
#include <QByteArray>
#include <QIODevice>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QMediaDevices>
#include <QAudioFormat>
#include <QDebug>
#include <iostream>
#include <atomic>

class AudioInput : public QObject
{
    Q_OBJECT

public:
    explicit AudioInput(QObject *parent = nullptr) : QObject(parent), audioSource(nullptr), audioInputDevice(nullptr)
    {

    }

    ~AudioInput()
    {        
        stop();
        if (audioSource) {
            delete audioSource;
        }
    }

    void initialize()
    {        
        try {
            QAudioFormat format;
            format.setSampleRate(44100);
            format.setChannelCount(1);
            format.setSampleFormat(QAudioFormat::Int16);

            QAudioDevice device = QMediaDevices::defaultAudioInput();
            if (device.isNull()) {
                std::cout << "No audio input device available" << std::endl;
                return;
            }

            qDebug() << device.description();

            audioSource = new QAudioSource(device, format, this);
            if (!audioSource) {
                std::cout << "Failed to create QAudioSource" << std::endl;
                return;
            }

            std::cout << "AudioInput::initialize completed successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Exception in AudioInput::initialize: " << e.what() << std::endl;
        }
    }

    void start()
    {        
        if (!audioSource) {
            std::cerr << "AudioInput::start failed: audioSource is null" << std::endl;
            return;
        }

        audioInputDevice = audioSource->start();
        if (audioInputDevice) {
            std::cout << "Audio input device started successfully" << std::endl;
            connect(audioInputDevice, &QIODevice::readyRead, this, &AudioInput::onAudioDataAvailable);
        } else {
            std::cerr << "Failed to start audio input device" << std::endl;
        }
    }

    void stop()
    {        
        if (audioSource) {
            audioSource->stop();
        }
    }

signals:
    void audioDataReady(const QByteArray &data);

private slots:
    void onAudioDataAvailable()
    {        
        if (audioInputDevice) {
            QByteArray newData = audioInputDevice->readAll();            
            emit audioDataReady(newData);
        } else {
            std::cerr << "audioInputDevice is null in onAudioDataAvailable" << std::endl;
        }
    }

private:
    QAudioSource *audioSource;
    QIODevice *audioInputDevice;
};

class AudioManager : public QObject
{
    Q_OBJECT

public:
    explicit AudioManager(std::atomic<bool>& abortFlag, QObject *parent = nullptr)
        : QObject(parent)
        , m_abortFlag(abortFlag)
        , m_audioThread(nullptr)
        , m_audioInput(nullptr)
    {

    }

    ~AudioManager()
    {        
        stopAudio();
        if (m_audioThread)
        {
            m_audioThread->wait();
            delete m_audioThread;
        }
        delete m_audioInput;
    }

    bool initialize()
    {
        try
        {            
            m_audioInput = new AudioInput();
            m_audioInput->initialize();  // This might throw an exception

            m_audioThread = new QThread(this);
            if (!m_audioThread) {
                throw std::runtime_error("Failed to create QThread");
            }

            m_audioInput->moveToThread(m_audioThread);

            connect(m_audioThread, &QThread::started, m_audioInput, &AudioInput::start);
            connect(m_audioThread, &QThread::finished, m_audioInput, &AudioInput::deleteLater);
            connect(m_audioInput, &AudioInput::audioDataReady, this, &AudioManager::handleAudioData, Qt::QueuedConnection);

            m_audioThread->start();
            if (!m_audioThread->isRunning()) {
                throw std::runtime_error("Failed to start audio thread");
            }

            std::cout << "AudioManager initialization completed" << std::endl;
            return true;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Exception in AudioManager::initialize: " << e.what() << std::endl;
            return false;
        }
        catch (...)
        {
            std::cerr << "Unknown exception in AudioManager::initialize" << std::endl;
            return false;
        }
    }

    const int16_t* getBuffer(int& size)
    {
        QMutexLocker locker(&m_mutex);
        while (m_buffer.size() < 4096 && !m_abortFlag.load())
        {
            if (!m_bufferNotEmpty.wait(&m_mutex, 100))
            {
                std::cout << "Wait timeout in getBuffer" << std::endl;
            }
        }
        if (m_abortFlag.load())
        {
            size = 0;
            return nullptr;
        }
        size = m_buffer.size();
        return reinterpret_cast<const int16_t*>(m_buffer.constData());
    }

    void clearBuffer()
    {
        QMutexLocker locker(&m_mutex);
        m_buffer.clear();
    }

    void stopAudio()
    {
        std::cout << "Stopping audio" << std::endl;
        if (m_audioInput)
        {
            m_audioInput->stop();
        }
        if (m_audioThread)
        {
            m_audioThread->quit();
            if (!m_audioThread->wait(5000)) // 5 seconds timeout
            {
                std::cerr << "Audio thread did not stop in time" << std::endl;
                m_audioThread->terminate();
            }
        }
        std::cout << "Stopped audio" << std::endl;
    }

public slots:
    void handleAudioData(const QByteArray &data)
    {
        std::cout << "handleAudioData : " << data.size() << std::endl;
        QMutexLocker locker(&m_mutex);
        if (!m_abortFlag.load())
        {
            m_buffer.append(data);
            m_bufferNotEmpty.wakeAll();
        }
    }

private:
    QByteArray m_buffer;
    QMutex m_mutex;
    QWaitCondition m_bufferNotEmpty;
    std::atomic<bool>& m_abortFlag;
    QThread *m_audioThread;
    AudioInput *m_audioInput;
};

#endif // AUDIOINPUT_H
