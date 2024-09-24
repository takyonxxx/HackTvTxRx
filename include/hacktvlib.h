#ifndef HACKTVLIB_H
#define HACKTVLIB_H

#include <QStringList>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <stdint.h>
#include <vector>
#include <mutex>

class HackTvLib{
public:
    using LogCallback = std::function<void(const std::string&)>;
    using DataCallback = std::function<void(const int8_t*, size_t)>;

    HackTvLib();
    ~HackTvLib();
    bool start();
    bool stop();
    void setLogCallback(LogCallback callback);
    void setReceivedDataCallback(DataCallback callback);
    bool setArguments(const std::vector<std::string>& args);
    void setMicEnabled(bool newMicEnabled);
    void setFrequency(uint64_t frequency_hz);
    void setSampleRate(uint32_t sample_rate);
    void setAmplitude(float newAmplitude);
    void setFilter_size(float newFilter_size);
    void setModulation_index(float newModulation_index);
    void setDecimation(int newDecimation);

private slots:
    void emitReceivedData(const int8_t *data, size_t data_len);
    void dataReceived(const int8_t* data, size_t data_len);
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
    bool micEnabled = false;
    void log(const char* format, ...);
    void cleanupArgv();
    void rfTxLoop();
    void rfRxLoop();
};

#endif // HACKTVLIB_H
