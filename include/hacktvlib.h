#ifndef HACKTVLIB_H
#define HACKTVLIB_H
#ifdef _WIN32
    #ifdef HACKTVLIB_LIBRARY
        #define HACKTVLIB_EXPORT __declspec(dllexport)
    #else
        #define HACKTVLIB_EXPORT __declspec(dllimport)
    #endif
#else
    #define HACKTVLIB_EXPORT  // Linux için boş
#endif
#include <QStringList>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <stdint.h>
#include <vector>
#include <mutex>

class HACKTVLIB_EXPORT HackTvLib 
{
public:
    using LogCallback = std::function<void(const std::string&)>;
    using DataCallback = std::function<void(const int8_t*, size_t)>;

    HackTvLib();
    ~HackTvLib();
    bool start();
    bool stop();
    void setLogCallback(LogCallback callback);
    void setReceivedDataCallback(DataCallback callback);
    void clearCallbacks();
    void resetLogCallback();
    void resetReceivedDataCallback();
    bool setArguments(const std::vector<std::string>& args);
    void setMicEnabled(bool newMicEnabled);
    void setFrequency(uint64_t frequency_hz);
    void setSampleRate(uint32_t sample_rate);
    void setAmplitude(float newAmplitude);
    void setFilter_size(float newFilter_size);
    void setModulation_index(float newModulation_index);
    void setDecimation(int newDecimation);
    void setInterpolation(float newInterpolation);
    void setLnaGain(unsigned int lna_gain);
    void setVgaGain(unsigned int vga_gain);
    void setTxAmpGain(unsigned int tx_amp_gain);
    void setRxAmpGain(unsigned int rx_amp_gain);

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
