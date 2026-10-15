#ifndef WORKERQUEUE_H
#define WORKERQUEUE_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

/*
 * One dedicated worker thread with a bounded job queue.
 *
 * Used for the heavy per-frame stages (PAL video decode, PAL audio demod)
 * so that each stage runs on its own thread with back-pressure instead of
 * being thrown at a shared QThreadPool:
 *   - post(job)                      queue it; if the queue is full the OLDEST job is dropped
 *   - post(job, dropIfBusy = true)   skip the job entirely if the worker is still busy
 *
 * The destructor stops the thread and joins it. Jobs still queued are discarded.
 */
class WorkerQueue
{
public:
    explicit WorkerQueue(std::string name, size_t maxDepth = 4)
        : m_name(std::move(name))
        , m_maxDepth(maxDepth)
        , m_thread([this] { loop(); })
    {
    }

    ~WorkerQueue()
    {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_quit = true;
            m_jobs.clear();
        }
        m_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    WorkerQueue(const WorkerQueue&) = delete;
    WorkerQueue& operator=(const WorkerQueue&) = delete;

    /* Returns false if the job was dropped. */
    bool post(std::function<void()> job, bool dropIfBusy = false)
    {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_quit) return false;
            if (dropIfBusy && (m_busy.load(std::memory_order_acquire) || !m_jobs.empty()))
                return false;
            if (m_jobs.size() >= m_maxDepth) {
                m_jobs.pop_front();
                ++m_dropped;
            }
            m_jobs.push_back(std::move(job));
        }
        m_cv.notify_one();
        return true;
    }

    bool   busy()    const { return m_busy.load(std::memory_order_acquire); }
    size_t dropped() const { return m_dropped.load(std::memory_order_relaxed); }

    size_t pending() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_jobs.size();
    }

    /* Discard queued jobs (the running one finishes). */
    void clear()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_jobs.clear();
    }

    /* Block until the queue is empty and the worker is idle. */
    void drain()
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_idleCv.wait(lk, [this] { return m_jobs.empty() && !m_busy.load(); });
    }

private:
    void loop()
    {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait(lk, [this] { return m_quit || !m_jobs.empty(); });
                if (m_quit) return;
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
                m_busy.store(true, std::memory_order_release);
            }
            try { job(); } catch (...) { /* never let a stage kill the worker */ }
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_busy.store(false, std::memory_order_release);
            }
            m_idleCv.notify_all();
        }
    }

    std::string m_name;
    size_t m_maxDepth;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_idleCv;
    std::deque<std::function<void()>> m_jobs;
    std::atomic<bool> m_busy{false};
    std::atomic<size_t> m_dropped{0};
    bool m_quit = false;
    std::thread m_thread;
};

#endif // WORKERQUEUE_H
