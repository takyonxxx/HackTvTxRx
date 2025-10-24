#ifndef RTLSDRDEVICE_H
#define RTLSDRDEVICE_H

#include <QObject>
#include <QByteArray>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <rtl-sdr.h>
#include "constants.h"

class RTLSDRDevice : public QObject
{
    Q_OBJECT

public:
    explicit RTLSDRDevice(QObject *parent = nullptr);
    ~RTLSDRDevice();

    using DataCallback = std::function<void(const int8_t*, size_t)>;

    // Device enumeration
    static std::vector<std::string> listDevices();
    static int getDeviceCount();

    // Initialization
    bool initialize(uint32_t deviceIndex = 0,
                    uint32_t sampleRate = DEFAULT_SAMPLE_RATE,
                    uint32_t frequency = DEFAULT_FREQUENCY,
                    int gain = 0);
    bool isInitialized() const;

    // Control
    bool start();
    bool stop();
    bool isRunning() const;

    // Callbacks
    void setDataCallback(DataCallback callback);

    // Parameter setters (thread-safe)
    void setFrequency(uint64_t frequency);
    void setSampleRate(uint32_t sampleRate);
    void setGain(int gain);
    void setAutoGain(bool enable);
    void setAgcMode(bool enable);
    void setDirectSampling(int mode);
    void setOffsetTuning(bool enable);
    void setFrequencyCorrection(int ppm);
    void setBandwidth(uint32_t bandwidth);

    // Parameter getters
    uint64_t getFrequency() const;
    uint32_t getSampleRate() const;
    int getGain() const;
    bool getAutoGain() const;
    std::vector<int> getGains() const;

    // Device info
    std::string getDeviceName() const;
    uint32_t getDeviceIndex() const;

signals:
    void dataReady(const QByteArray &data);
    void error(const QString &message);
    void stateChanged(bool running);

private:
    // Callback
    static void rtlsdrCallback(unsigned char *buf, uint32_t len, void *ctx);

    // Helper functions
    void cleanup();
    bool applySettings();
    void emitError(const std::string& msg);

    // Thread management
    std::unique_ptr<std::thread> m_readThread;
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isDestroying;
    std::atomic<bool> m_isInitialized;

    // Mutex for thread safety
    mutable std::mutex m_deviceMutex;

    // RTL-SDR device handle
    rtlsdr_dev_t *m_device;

    // Callback
    DataCallback m_dataCallback;

    // Device info
    uint32_t m_deviceIndex;
    std::string m_deviceName;

    // Parameters (atomic for thread safety)
    std::atomic<uint64_t> m_frequency;
    std::atomic<uint32_t> m_sampleRate;
    std::atomic<int> m_gain;
    std::atomic<bool> m_autoGain;
    std::atomic<bool> m_agcMode;
    std::atomic<int> m_directSampling;
    std::atomic<bool> m_offsetTuning;
    std::atomic<int> m_freqCorrection;
    std::atomic<uint32_t> m_bandwidth;
};

#endif // RTLSDRDEVICE_H
