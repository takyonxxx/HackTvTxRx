#ifndef PALPROCESSORTHREAD_H
#define PALPROCESSORTHREAD_H

#include <QThread>
#include <QImage>
#include <atomic>
#include "CircularBuffer.h"
#include "PALDecoder.h"

class PALProcessorThread : public QThread
{
    Q_OBJECT

public:
    explicit PALProcessorThread(CircularBuffer* buffer, PALDecoder* decoder, QObject* parent = nullptr)
        : QThread(parent)
        , m_buffer(buffer)
        , m_decoder(decoder)
        , m_running(false)
        , m_processedSamples(0)
    {
    }

    void stopProcessing()
    {
        m_running.store(false, std::memory_order_release);
    }

    uint64_t processedSamples() const
    {
        return m_processedSamples.load(std::memory_order_relaxed);
    }

signals:
    void bufferStats(size_t available, uint64_t dropped);

protected:
    void run() override
    {
        m_running.store(true, std::memory_order_release);

        // Processing buffer - process in chunks
        constexpr size_t CHUNK_SIZE = 262144; // Same as HackRF callback size
        std::vector<int8_t> processingBuffer(CHUNK_SIZE);

        uint64_t statsCounter = 0;

        while (m_running.load(std::memory_order_acquire)) {
            // Read available data from circular buffer
            size_t available = m_buffer->availableData();

            if (available >= CHUNK_SIZE) {
                // Process full chunk
                size_t bytesRead = m_buffer->read(processingBuffer.data(), CHUNK_SIZE);

                if (bytesRead > 0 && m_decoder) {
                    m_decoder->processSamples(processingBuffer.data(), bytesRead);
                    m_processedSamples.fetch_add(bytesRead / 2, std::memory_order_relaxed);
                }
            } else if (available > 0 && available < CHUNK_SIZE) {
                // Process partial data if available
                size_t bytesRead = m_buffer->read(processingBuffer.data(), available);

                if (bytesRead > 0 && m_decoder) {
                    m_decoder->processSamples(processingBuffer.data(), bytesRead);
                    m_processedSamples.fetch_add(bytesRead / 2, std::memory_order_relaxed);
                }
            } else {
                // No data available, sleep briefly
                QThread::usleep(100); // 0.1ms
            }

            // Emit stats periodically
            statsCounter++;
            if (statsCounter % 100 == 0) {
                emit bufferStats(m_buffer->availableData(), m_buffer->droppedFrames());
            }
        }
    }

private:
    CircularBuffer* m_buffer;
    PALDecoder* m_decoder;
    std::atomic<bool> m_running;
    std::atomic<uint64_t> m_processedSamples;
};

#endif // PALPROCESSORTHREAD_H
