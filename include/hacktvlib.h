#ifndef HACKTVLIB_H
#define HACKTVLIB_H

#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

class HackTvLib{
public:
    using LogCallback = std::function<void(const std::string&)>;
    using DataCallback = std::function<void(const int16_t*, size_t)>;

    HackTvLib();
    ~HackTvLib();
    bool start();
    bool stop();
    void setLogCallback(LogCallback callback);
    void setReceivedDataCallback(DataCallback callback);
    bool setArguments(const std::vector<std::string>& args);

private:
    LogCallback m_logCallback;
    DataCallback m_dataCallback;
    std::thread m_thread;
    std::mutex m_mutex;
    std::atomic<bool> m_abort;
    std::atomic<int> m_signal;
    std::vector<char*> m_argv;
    bool openDevice();
    bool setVideo();
    bool initAv();
    bool parseArguments();
    void log(const char* format, ...);
    void cleanupArgv();
    void rfTxLoop();
    void rfRxLoop();
    void emitReceivedData(const int16_t* data, size_t samples);
};

#endif // HACKTVLIB_H
