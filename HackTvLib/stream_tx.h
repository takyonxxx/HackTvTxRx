#ifndef STREAM_TX_H
#define STREAM_TX_H
#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstring>
#include <memory>

namespace dsp::buffer {
template<class T>
inline T* alloc_tx(int count) {
    return new T[count];
}

template<class T>
inline void clear_tx(T* buffer, int count, int offset = 0) {
    std::memset(&buffer[offset], 0, count * sizeof(T));
}

inline void free_tx(void* buffer) {
    delete[] static_cast<char*>(buffer);
}
}

// 1MSample buffer
#define STREAM_BUFFER_SIZE 1000000

namespace dsp {
class untyped_stream_tx {
public:
    virtual ~untyped_stream_tx() {}
    virtual bool swap(int size) { return false; }
    virtual void flush() {}
};

template <class T>
class stream_tx : public untyped_stream_tx {
public:
    stream_tx() : writeBuf(buffer::alloc_tx<T>(STREAM_BUFFER_SIZE)),
        readBuf(buffer::alloc_tx<T>(STREAM_BUFFER_SIZE)) {}

    virtual ~stream_tx() {
        free();
    }

    stream_tx(const stream_tx&) = delete;
    stream_tx& operator=(const stream_tx&) = delete;

    virtual void setBufferSize(int samples) {
        std::lock_guard<std::mutex> lock(bufferMutex);
        buffer::free_tx(writeBuf);
        buffer::free_tx(readBuf);
        writeBuf = buffer::alloc_tx<T>(samples);
        readBuf = buffer::alloc_tx<T>(samples);
    }

    virtual bool swap(int size) override {
        std::lock_guard<std::mutex> lock(bufferMutex);
        dataSize.store(size);
        std::swap(writeBuf, readBuf);
        canSwap.store(false);
        return true;
    }

    std::vector<float> readBufferToVector() {
        std::vector<float> result;
        int currentSize = dataSize.load();
        std::lock_guard<std::mutex> lock(bufferMutex);
        if (currentSize <= 0 || readBuf == nullptr) {
            return result;
        }
        result.reserve(currentSize * 2);
        for (int i = 0; i < currentSize; ++i) {
            result.push_back(readBuf[i].re);
            result.push_back(readBuf[i].im);
        }
        return result;
    }

    void free() {
        std::lock_guard<std::mutex> lock(bufferMutex);
        if (writeBuf) { buffer::free_tx(writeBuf); writeBuf = nullptr; }
        if (readBuf) { buffer::free_tx(readBuf); readBuf = nullptr; }
    }

public:
    T* writeBuf;
    T* readBuf;
    std::mutex bufferMutex;
    std::atomic<int> dataSize{0};
    std::atomic<bool> canSwap{true};
    std::atomic<bool> dataReady{false};
    std::atomic<bool> readerStop{false};
    std::atomic<bool> writerStop{false};
    std::condition_variable swapCV;
    std::condition_variable rdyCV;
};
}

#endif // STREAM_TX_H
