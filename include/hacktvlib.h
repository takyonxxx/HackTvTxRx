#ifndef HACKTVLIB_H
#define HACKTVLIB_H

#ifdef _WIN32
#ifdef HACKTVLIB_LIBRARY
#define HACKTVLIB_EXPORT __declspec(dllexport)
#else
#define HACKTVLIB_EXPORT __declspec(dllimport)
#endif
#else
#define HACKTVLIB_EXPORT
#endif

#include <QObject>
#include <QStringList>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <stdint.h>
#include <vector>
#include <mutex>
#include <cstring>

// Forward declarations for types defined in DLL
struct hacktv_t;
class HackRfDevice;
class RTLSDRDevice;
enum rxtx_mode : int;

class HACKTVLIB_EXPORT HackTvLib : public QObject
{
    Q_OBJECT

public:
    explicit HackTvLib(QObject *parent = nullptr);
    ~HackTvLib();

    using LogCallback = std::function<void(const std::string&)>;
    using DataCallback = std::function<void(const int8_t*, size_t)>;

    bool start();
    bool stop();
    int hardReset();
    void setLogCallback(LogCallback callback);
    void setReceivedDataCallback(DataCallback callback);
    void clearCallbacks();
    bool setArguments(const std::vector<std::string>& args);
    void setMicEnabled(bool newMicEnabled);
    void setAudioFilePath(const std::string& filePath, bool loop = true);
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

    // RTL-SDR specific
    void setFreqCorrection(int ppm);
    void setDirectSampling(int mode);
    void setOffsetTuning(bool enable);

    bool isInitialized() const {
        return (s != nullptr);
    }

    bool isDeviceReady() const {
        return (hackRfDevice != nullptr);
    }

private slots:
    void emitReceivedData(const int8_t *data, size_t data_len);
    void dataReceived(const int8_t* data, size_t data_len);

private:
    // ========================================
    // MEMBER ORDER MUST MATCH DLL EXACTLY
    // ========================================

    // Callbacks
    LogCallback m_logCallback;
    DataCallback m_dataCallback;

    // Threading
    std::thread m_txThread;
    std::mutex m_mutex;
    std::atomic<bool> m_abort{false};
    std::atomic<int> m_signal{0};

    // Arguments
    std::vector<char*> m_argv;

    // Configuration
    hacktv_t* s = nullptr;
    rxtx_mode m_rxTxMode;
    bool micEnabled = false;
    std::string m_audioFilePath;
    bool m_audioFileLoop = true;

    // Devices
    HackRfDevice* hackRfDevice = nullptr;
    RTLSDRDevice* rtlSdrDevice = nullptr;

    // Private methods
    bool openDevice();
    bool setVideo();
    bool initAv();
    bool parseArguments();
    void log(const char* format, ...);
    void cleanupArgv();
    void rfTxLoop();
    void rfRxLoop();
};

#endif // HACKTVLIB_H
