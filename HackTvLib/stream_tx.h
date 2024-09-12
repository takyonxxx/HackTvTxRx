#ifndef STREAM_TX_H
#define STREAM_TX_H
#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdlib>
#include <cstring>

#if defined(_MSC_VER)  // Check if compiling on Windows (MSVC)
#include <malloc.h>  // for _aligned_malloc and _aligned_free
#elif defined(__unix__) || defined(__APPLE__)
#include <malloc.h>  // for posix_memalign on Unix-like systems
#endif

namespace dsp::buffer {

template<class T>
inline T* alloc_tx(int count, std::size_t alignment = alignof(T)) {
#if defined(_MSC_VER)  // Windows (MSVC)
    return static_cast<T*>(_aligned_malloc(count * sizeof(T), alignment));

#elif defined(__unix__) || defined(__APPLE__)  // For Linux/macOS
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, count * sizeof(T)) != 0) {
        return nullptr;  // Return nullptr on failure
    }
    return static_cast<T*>(ptr);

#else  // For other platforms, fallback to std::malloc (manual alignment)
    void* raw_ptr = std::malloc(count * sizeof(T) + alignment - 1 + sizeof(void*));
    if (!raw_ptr) return nullptr;

    // Align the pointer manually
    void* aligned_ptr = reinterpret_cast<void*>(
        (reinterpret_cast<std::uintptr_t>(raw_ptr) + sizeof(void*) + alignment - 1) & ~(alignment - 1));

    // Store the original raw pointer just before the aligned memory block
    reinterpret_cast<void**>(aligned_ptr)[-1] = raw_ptr;

    return static_cast<T*>(aligned_ptr);
#endif
}

// Clears the buffer by setting the memory to zero
template<class T>
inline void clear_tx(T* buffer, int count, int offset = 0) {
    std::memset(&buffer[offset], 0, count * sizeof(T));
}

// Platform-independent memory deallocation
inline void free_tx(void* buffer) {
#if defined(_MSC_VER)  // Windows (MSVC)
    _aligned_free(buffer);

#elif defined(__unix__) || defined(__APPLE__)  // For Linux/macOS
    std::free(buffer);

#else  // For other platforms (manual alignment)
    std::free(reinterpret_cast<void**>(buffer)[-1]);
#endif
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
    stream_tx()
        : writeBuf(buffer::alloc_tx<T>(STREAM_BUFFER_SIZE)),
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
        std::unique_lock<std::mutex> lock(bufferMutex);
        // Wait until swap is allowed
        swapCV.wait(lock, [this] { return canSwap.load(); });

        dataSize.store(size);
        std::swap(writeBuf, readBuf);
        canSwap.store(false);
        swapCV.notify_all();
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
        if (writeBuf) {
            buffer::free_tx(writeBuf);
            writeBuf = nullptr;
        }
        if (readBuf) {
            buffer::free_tx(readBuf);
            readBuf = nullptr;
        }
    }
public:
    T* writeBuf;
    T* readBuf;
private:
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
