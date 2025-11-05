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
    qDebug() << "AudioProcessorThread created";
}

AudioProcessorThread::~AudioProcessorThread()
{
    stopProcessing();
    qDebug() << "AudioProcessorThread destroyed";
}

void AudioProcessorThread::stopProcessing()
{
    m_running = false;
}

void AudioProcessorThread::run()
{
    qDebug() << "AudioProcessorThread::run() STARTED on thread:" << QThread::currentThread();

    std::vector<int8_t> chunk(CHUNK_SIZE);
    uint64_t iterationCount = 0;
    uint64_t totalBytesRead = 0;

    while (m_running) {
        iterationCount++;

        // Debug every 1000 iterations
        if (iterationCount % 1000 == 0) {
            qDebug() << "AudioProcessorThread: iteration" << iterationCount
                     << "bytes read:" << totalBytesRead
                     << "buffer available:" << m_buffer->availableData();
        }

        // Check if data available
        size_t available = m_buffer->availableData();

        if (available < CHUNK_SIZE) {
            // Not enough data, wait
            if (iterationCount % 1000 == 0) {
                qDebug() << "AudioProcessorThread: Waiting for data, available:" << available;
            }
            QThread::msleep(5);
            continue;
        }

        // Read chunk
        size_t bytesRead = m_buffer->read(chunk.data(), CHUNK_SIZE);

        if (bytesRead > 0) {
            totalBytesRead += bytesRead;

            // Process audio
            m_demodulator->processSamples(chunk.data(), bytesRead);
        } else {
            qDebug() << "AudioProcessorThread: Read returned 0 bytes!";
        }
    }

    qDebug() << "AudioProcessorThread::run() STOPPED. Total bytes processed:" << totalBytesRead;
}
