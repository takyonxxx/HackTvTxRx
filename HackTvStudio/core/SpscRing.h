#ifndef SPSCRING_H
#define SPSCRING_H

#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

/*
 * Lock-free single-producer / single-consumer ring buffer.
 *
 * Producer: the libhackrf USB transfer thread (writes raw int8 IQ).
 * Consumer: the RX DSP thread (RxEngine) or the TX modulator thread.
 *
 * Monotonic read/write counters: index = counter & mask, so the ring never
 * needs a "full vs empty" flag. Capacity must be a power of two.
 */
template <typename T>
class SpscRing
{
public:
    explicit SpscRing(size_t capacityPow2)
        : m_mask(capacityPow2 - 1)
        , m_buf(capacityPow2)
    {
        // capacityPow2 must be a power of two
        static_assert(std::is_trivially_copyable<T>::value, "SpscRing needs a POD element type");
    }

    size_t capacity()  const { return m_mask + 1; }

    size_t available() const
    {
        return m_w.load(std::memory_order_acquire) - m_r.load(std::memory_order_acquire);
    }

    size_t freeSpace() const { return capacity() - available(); }

    /* Producer side. Returns the number of elements actually written. */
    size_t write(const T* src, size_t n)
    {
        const size_t w = m_w.load(std::memory_order_relaxed);
        const size_t r = m_r.load(std::memory_order_acquire);
        const size_t space = capacity() - (w - r);
        if (n > space) n = space;
        if (n == 0) return 0;

        const size_t idx   = w & m_mask;
        const size_t first = std::min(n, capacity() - idx);
        std::memcpy(&m_buf[idx], src, first * sizeof(T));
        if (n > first)
            std::memcpy(&m_buf[0], src + first, (n - first) * sizeof(T));

        m_w.store(w + n, std::memory_order_release);
        return n;
    }

    /* Consumer side. Returns the number of elements actually read. */
    size_t read(T* dst, size_t n)
    {
        const size_t r = m_r.load(std::memory_order_relaxed);
        const size_t w = m_w.load(std::memory_order_acquire);
        const size_t avail = w - r;
        if (n > avail) n = avail;
        if (n == 0) return 0;

        const size_t idx   = r & m_mask;
        const size_t first = std::min(n, capacity() - idx);
        std::memcpy(dst, &m_buf[idx], first * sizeof(T));
        if (n > first)
            std::memcpy(dst + first, &m_buf[0], (n - first) * sizeof(T));

        m_r.store(r + n, std::memory_order_release);
        return n;
    }

    /* Consumer side: drop everything that is currently buffered. */
    void clear()
    {
        m_r.store(m_w.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    const size_t m_mask;
    std::vector<T> m_buf;

    // Keep the two counters on separate cache lines
    alignas(64) std::atomic<size_t> m_w{0};
    alignas(64) std::atomic<size_t> m_r{0};
};

#endif // SPSCRING_H
