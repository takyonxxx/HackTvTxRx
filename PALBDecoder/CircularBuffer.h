#ifndef CIRCULARBUFFER_H
#define CIRCULARBUFFER_H

#include <vector>
#include <atomic>
#include <cstdint>
#include <cstring>

// Lock-free circular buffer for IQ samples
class CircularBuffer
{
public:
    explicit CircularBuffer(size_t capacity)
        : m_capacity(capacity)
        , m_buffer(capacity)
        , m_writePos(0)
        , m_readPos(0)
        , m_droppedFrames(0)
    {
    }

    // Write data to buffer (called from HackRF callback thread)
    bool write(const int8_t* data, size_t len)
    {
        size_t currentWrite = m_writePos.load(std::memory_order_relaxed);
        size_t currentRead = m_readPos.load(std::memory_order_acquire);

        size_t available = getAvailableWrite(currentWrite, currentRead);

        if (len > available) {
            // Buffer full - drop oldest data
            m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        // Write in two parts if wrapping around
        size_t firstPart = std::min(len, m_capacity - currentWrite);
        std::memcpy(m_buffer.data() + currentWrite, data, firstPart);

        if (firstPart < len) {
            std::memcpy(m_buffer.data(), data + firstPart, len - firstPart);
        }

        size_t newWrite = (currentWrite + len) % m_capacity;
        m_writePos.store(newWrite, std::memory_order_release);

        return true;
    }

    // Read data from buffer (called from worker thread)
    size_t read(int8_t* data, size_t maxLen)
    {
        size_t currentRead = m_readPos.load(std::memory_order_relaxed);
        size_t currentWrite = m_writePos.load(std::memory_order_acquire);

        size_t available = getAvailableRead(currentRead, currentWrite);
        size_t toRead = std::min(maxLen, available);

        if (toRead == 0) {
            return 0;
        }

        // Read in two parts if wrapping around
        size_t firstPart = std::min(toRead, m_capacity - currentRead);
        std::memcpy(data, m_buffer.data() + currentRead, firstPart);

        if (firstPart < toRead) {
            std::memcpy(data + firstPart, m_buffer.data(), toRead - firstPart);
        }

        size_t newRead = (currentRead + toRead) % m_capacity;
        m_readPos.store(newRead, std::memory_order_release);

        return toRead;
    }

    size_t availableData() const
    {
        size_t currentRead = m_readPos.load(std::memory_order_relaxed);
        size_t currentWrite = m_writePos.load(std::memory_order_acquire);
        return getAvailableRead(currentRead, currentWrite);
    }

    void clear()
    {
        m_readPos.store(0, std::memory_order_release);
        m_writePos.store(0, std::memory_order_release);
    }

    uint64_t droppedFrames() const
    {
        return m_droppedFrames.load(std::memory_order_relaxed);
    }

private:
    size_t getAvailableRead(size_t read, size_t write) const
    {
        if (write >= read) {
            return write - read;
        }
        return m_capacity - read + write;
    }

    size_t getAvailableWrite(size_t write, size_t read) const
    {
        if (read > write) {
            return read - write - 1;
        }
        return m_capacity - (write - read) - 1;
    }

    const size_t m_capacity;
    std::vector<int8_t> m_buffer;
    std::atomic<size_t> m_writePos;
    std::atomic<size_t> m_readPos;
    std::atomic<uint64_t> m_droppedFrames;
};

#endif // CIRCULARBUFFER_H
