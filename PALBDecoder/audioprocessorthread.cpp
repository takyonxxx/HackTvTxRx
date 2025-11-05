#include "AudioProcessorThread.h"
#include <QDebug>

AudioProcessorThread::AudioProcessorThread(CircularBuffer* buffer,
                                           AudioDemodulator* demodulator,
                                           QObject* parent)
    : QThread(parent)
    , m_buffer(buffer)
    , m_demodulator(demodulator)
    , m_running(true)
{
}

AudioProcessorThread::~AudioProcessorThread()
{
    stopProcessing();
}

void AudioProcessorThread::stopProcessing()
{
    m_running = false;
}

void AudioProcessorThread::run()
{
    qDebug() << "AudioProcessorThread started";

    std::vector<int8_t> chunk(CHUNK_SIZE);

    while (m_running) {
        // Check if data available
        if (m_buffer->availableData() < CHUNK_SIZE) {
            QThread::msleep(5);  // Wait a bit
            continue;
        }

        // Read chunk
        size_t bytesRead = m_buffer->read(chunk.data(), CHUNK_SIZE);

        if (bytesRead > 0) {
            // Process audio
            m_demodulator->processSamples(chunk.data(), bytesRead);
        }
    }

    qDebug() << "AudioProcessorThread stopped";
}
