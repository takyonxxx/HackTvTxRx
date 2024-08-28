#ifndef HACKTVLIB_H
#define HACKTVLIB_H

#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

class HackTvLib{
public:
    using LogCallback = std::function<void(const std::string&)>;

    HackTvLib();
    ~HackTvLib();
    bool start();
    bool stop();
    void setLogCallback(LogCallback callback);
    bool setArguments(const std::vector<std::string>& args);

private:
    LogCallback m_logCallback;
    std::thread m_thread;
    std::atomic<bool> m_abort;
    std::atomic<int> m_signal;
    std::vector<char*> m_argv;
    void log(const char* format, ...);
    void cleanupArgv();
    void rfLoop();
};

#endif // HACKTVLIB_H
