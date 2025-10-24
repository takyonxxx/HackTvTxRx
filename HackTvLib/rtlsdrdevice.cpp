#include "rtlsdrdevice.h"
#include <iostream>
#include <sstream>
#include <chrono>

RTLSDRDevice::RTLSDRDevice(QObject *parent)
    : QObject(parent)
    , m_device(nullptr)
    , m_isRunning(false)
    , m_isDestroying(false)
    , m_isInitialized(false)
    , m_deviceIndex(0)
    , m_frequency(DEFAULT_FREQUENCY)
    , m_sampleRate(DEFAULT_SAMPLE_RATE)
    , m_gain(0)
    , m_autoGain(true)
    , m_agcMode(false)
    , m_directSampling(0)
    , m_offsetTuning(false)
    , m_freqCorrection(0)
    , m_bandwidth(0)
{
}

RTLSDRDevice::~RTLSDRDevice()
{
    m_isDestroying.store(true);

    // Stop the device if running
    if (m_isRunning.load()) {
        stop();
    }

    // Wait for thread to finish
    if (m_readThread && m_readThread->joinable()) {
        m_readThread->join();
    }

    // Cleanup device
    cleanup();
}

std::vector<std::string> RTLSDRDevice::listDevices()
{
    std::vector<std::string> devices;
    int count = rtlsdr_get_device_count();

    for (int i = 0; i < count; ++i) {
        char manufacturer[256] = {0};
        char product[256] = {0};
        char serial[256] = {0};

        if (rtlsdr_get_device_usb_strings(i, manufacturer, product, serial) == 0) {
            std::stringstream ss;
            ss << "[" << i << "] " << manufacturer << " " << product;
            if (serial[0] != '\0') {
                ss << " (SN: " << serial << ")";
            }
            devices.push_back(ss.str());
        } else {
            const char* name = rtlsdr_get_device_name(i);
            if (name) {
                std::stringstream ss;
                ss << "[" << i << "] " << name;
                devices.push_back(ss.str());
            }
        }
    }

    return devices;
}

int RTLSDRDevice::getDeviceCount()
{
    return rtlsdr_get_device_count();
}

bool RTLSDRDevice::initialize(uint32_t deviceIndex, uint32_t sampleRate, uint32_t frequency, int gain)
{
    std::lock_guard<std::mutex> lock(m_deviceMutex);

    if (m_isInitialized.load()) {
        emitError("Device already initialized");
        return false;
    }

    // Check device count
    int deviceCount = rtlsdr_get_device_count();
    std::cout << "RTL-SDR device count: " << deviceCount << std::endl;

    if (deviceCount == 0) {
        emitError("No RTL-SDR devices found");
        return false;
    }

    if (deviceIndex >= static_cast<uint32_t>(deviceCount)) {
        emitError("Invalid device index");
        return false;
    }

    // Get device name
    const char* deviceName = rtlsdr_get_device_name(deviceIndex);
    m_deviceName = deviceName ? deviceName : "Unknown";
    std::cout << "Attempting to open device: " << m_deviceName << std::endl;

    // Open device
    int result = rtlsdr_open(&m_device, deviceIndex);
    if (result < 0) {
        std::stringstream ss;
        ss << "Failed to open RTL-SDR device. Error code: " << result;
        emitError(ss.str());
        return false;
    }

    m_deviceIndex = deviceIndex;
    m_sampleRate = sampleRate;
    m_frequency = frequency;
    m_gain = gain;
    m_autoGain = (gain == 0);

    // Apply initial settings
    if (!applySettings()) {
        cleanup();
        return false;
    }

    // Reset buffer
    result = rtlsdr_reset_buffer(m_device);
    if (result < 0) {
        std::cerr << "Warning: Failed to reset buffer" << std::endl;
    }

    m_isInitialized.store(true);
    std::cout << "Successfully initialized RTL-SDR device" << std::endl;
    return true;
}

bool RTLSDRDevice::applySettings()
{
    if (!m_device) {
        return false;
    }

    int result;

    // Set sample rate
    result = rtlsdr_set_sample_rate(m_device, m_sampleRate.load());
    if (result < 0) {
        std::cerr << "Failed to set sample rate. Error: " << result << std::endl;
        return false;
    }

    // Set center frequency
    result = rtlsdr_set_center_freq(m_device, m_frequency.load());
    if (result < 0) {
        std::cerr << "Failed to set frequency. Error: " << result << std::endl;
        return false;
    }

    // Set gain mode and gain
    if (m_autoGain.load()) {
        result = rtlsdr_set_tuner_gain_mode(m_device, 0); // Auto gain
    } else {
        result = rtlsdr_set_tuner_gain_mode(m_device, 1); // Manual gain
        if (result == 0) {
            result = rtlsdr_set_tuner_gain(m_device, m_gain.load());
        }
    }

    if (result < 0) {
        std::cerr << "Failed to set gain. Error: " << result << std::endl;
        return false;
    }

    // Set AGC mode
    result = rtlsdr_set_agc_mode(m_device, m_agcMode.load() ? 1 : 0);
    if (result < 0) {
        std::cerr << "Warning: Failed to set AGC mode" << std::endl;
    }

    // Set frequency correction
    if (m_freqCorrection.load() != 0) {
        result = rtlsdr_set_freq_correction(m_device, m_freqCorrection.load());
        if (result < 0) {
            std::cerr << "Warning: Failed to set frequency correction" << std::endl;
        }
    }

    // Set direct sampling
    if (m_directSampling.load() != 0) {
        result = rtlsdr_set_direct_sampling(m_device, m_directSampling.load());
        if (result < 0) {
            std::cerr << "Warning: Failed to set direct sampling" << std::endl;
        }
    }

    // Set offset tuning
    result = rtlsdr_set_offset_tuning(m_device, m_offsetTuning.load() ? 1 : 0);
    if (result < 0) {
        std::cerr << "Warning: Failed to set offset tuning" << std::endl;
    }

    // Set tuner bandwidth if specified
    if (m_bandwidth.load() > 0) {
        result = rtlsdr_set_tuner_bandwidth(m_device, m_bandwidth.load());
        if (result < 0) {
            std::cerr << "Warning: Failed to set tuner bandwidth" << std::endl;
        }
    }

    return true;
}

bool RTLSDRDevice::start()
{
    std::lock_guard<std::mutex> lock(m_deviceMutex);

    if (!m_device) {
        emitError("Device not initialized");
        return false;
    }

    if (m_isRunning.load()) {
        emitError("Device is already running");
        return false;
    }

    // Reset buffer before starting
    rtlsdr_reset_buffer(m_device);

    m_isRunning.store(true);

    // Start reading thread
    m_readThread = std::make_unique<std::thread>([this]() {
        int result = rtlsdr_read_async(m_device, rtlsdrCallback, this, 0, 0);
        if (result < 0 && !m_isDestroying.load()) {
            std::cerr << "rtlsdr_read_async failed with error: " << result << std::endl;
            m_isRunning.store(false);
            emit error(QString("Read async failed: %1").arg(result));
        }
    });

    std::cout << "RTL-SDR device started" << std::endl;
    emit stateChanged(true);
    return true;
}

bool RTLSDRDevice::stop()
{
    if (m_isDestroying.load()) {
        return true;
    }

    if (!m_isRunning.load()) {
        std::cout << "Device is not running" << std::endl;
        return false;
    }

    // Cancel async reading
    if (m_device) {
        int result = rtlsdr_cancel_async(m_device);
        if (result < 0) {
            std::cerr << "Warning: rtlsdr_cancel_async failed: " << result << std::endl;
        }
    }

    m_isRunning.store(false);

    // Wait for thread to finish
    if (m_readThread && m_readThread->joinable()) {
        // Use timed join to avoid infinite wait
        auto start = std::chrono::steady_clock::now();
        while (m_readThread->joinable()) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::seconds(5)) {
                std::cerr << "Warning: Timeout waiting for read thread" << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            if (m_readThread->joinable()) {
                m_readThread->join();
                break;
            }
        }
        m_readThread.reset();
    }

    std::cout << "RTL-SDR device stopped" << std::endl;
    emit stateChanged(false);
    return true;
}

void RTLSDRDevice::cleanup()
{
    std::lock_guard<std::mutex> lock(m_deviceMutex);

    if (m_device) {
        rtlsdr_close(m_device);
        m_device = nullptr;
        m_isInitialized.store(false);
    }
}

bool RTLSDRDevice::isInitialized() const
{
    return m_isInitialized.load();
}

bool RTLSDRDevice::isRunning() const
{
    return m_isRunning.load();
}

void RTLSDRDevice::rtlsdrCallback(unsigned char *buf, uint32_t len, void *ctx)
{
    RTLSDRDevice *device = static_cast<RTLSDRDevice*>(ctx);

    if (!device || device->m_isDestroying.load() || !device->m_isRunning.load()) {
        return;
    }

    try {
        // Convert to int8_t format
        const int8_t* int8Buf = reinterpret_cast<const int8_t*>(buf);

        // Call user callback
        if (device->m_dataCallback) {
            device->m_dataCallback(int8Buf, len);
        }

        // Emit Qt signal
        QByteArray data(reinterpret_cast<const char*>(buf), len);
        emit device->dataReady(data);

    } catch (const std::exception& e) {
        std::cerr << "Exception in RTL-SDR callback: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception in RTL-SDR callback" << std::endl;
    }
}

// Thread-safe setters
void RTLSDRDevice::setFrequency(uint64_t frequency)
{
    m_frequency.store(frequency);

    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (m_device && !m_isDestroying.load()) {
        int result = rtlsdr_set_center_freq(m_device, static_cast<uint32_t>(frequency));
        if (result < 0) {
            std::stringstream ss;
            ss << "Failed to set frequency to " << frequency << " Hz. Error: " << result;
            emitError(ss.str());
        } else {
            std::cout << "Frequency set to " << frequency << " Hz" << std::endl;
        }
    }
}

void RTLSDRDevice::setSampleRate(uint32_t sampleRate)
{
    m_sampleRate.store(sampleRate);

    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (m_device && !m_isDestroying.load()) {
        int result = rtlsdr_set_sample_rate(m_device, sampleRate);
        if (result < 0) {
            std::stringstream ss;
            ss << "Failed to set sample rate to " << sampleRate << " Hz. Error: " << result;
            emitError(ss.str());
        } else {
            std::cout << "Sample rate set to " << sampleRate << " Hz" << std::endl;
        }
    }
}

void RTLSDRDevice::setGain(int gain)
{
    m_gain.store(gain);
    m_autoGain.store(false);

    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (m_device && !m_isDestroying.load()) {
        rtlsdr_set_tuner_gain_mode(m_device, 1); // Manual mode
        int result = rtlsdr_set_tuner_gain(m_device, gain);
        if (result < 0) {
            std::stringstream ss;
            ss << "Failed to set gain to " << gain << ". Error: " << result;
            emitError(ss.str());
        }
    }
}

void RTLSDRDevice::setAutoGain(bool enable)
{
    m_autoGain.store(enable);

    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (m_device && !m_isDestroying.load()) {
        int result = rtlsdr_set_tuner_gain_mode(m_device, enable ? 0 : 1);
        if (result < 0) {
            emitError("Failed to set gain mode");
        }
    }
}

void RTLSDRDevice::setAgcMode(bool enable)
{
    m_agcMode.store(enable);

    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (m_device && !m_isDestroying.load()) {
        rtlsdr_set_agc_mode(m_device, enable ? 1 : 0);
    }
}

void RTLSDRDevice::setDirectSampling(int mode)
{
    m_directSampling.store(mode);

    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (m_device && !m_isDestroying.load()) {
        rtlsdr_set_direct_sampling(m_device, mode);
    }
}

void RTLSDRDevice::setOffsetTuning(bool enable)
{
    m_offsetTuning.store(enable);

    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (m_device && !m_isDestroying.load()) {
        rtlsdr_set_offset_tuning(m_device, enable ? 1 : 0);
    }
}

void RTLSDRDevice::setFrequencyCorrection(int ppm)
{
    m_freqCorrection.store(ppm);

    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (m_device && !m_isDestroying.load()) {
        rtlsdr_set_freq_correction(m_device, ppm);
    }
}

void RTLSDRDevice::setBandwidth(uint32_t bandwidth)
{
    m_bandwidth.store(bandwidth);

    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (m_device && !m_isDestroying.load()) {
        rtlsdr_set_tuner_bandwidth(m_device, bandwidth);
    }
}

// Getters
uint64_t RTLSDRDevice::getFrequency() const
{
    return m_frequency.load();
}

uint32_t RTLSDRDevice::getSampleRate() const
{
    return m_sampleRate.load();
}

int RTLSDRDevice::getGain() const
{
    return m_gain.load();
}

bool RTLSDRDevice::getAutoGain() const
{
    return m_autoGain.load();
}

std::vector<int> RTLSDRDevice::getGains() const
{
    std::vector<int> gains;

    std::lock_guard<std::mutex> lock(m_deviceMutex);
    if (m_device) {
        int count = rtlsdr_get_tuner_gains(m_device, nullptr);
        if (count > 0) {
            gains.resize(count);
            rtlsdr_get_tuner_gains(m_device, gains.data());
        }
    }

    return gains;
}

std::string RTLSDRDevice::getDeviceName() const
{
    return m_deviceName;
}

uint32_t RTLSDRDevice::getDeviceIndex() const
{
    return m_deviceIndex;
}

void RTLSDRDevice::setDataCallback(DataCallback callback)
{
    m_dataCallback = callback;
}

void RTLSDRDevice::emitError(const std::string& msg)
{
    std::cerr << "RTL-SDR Error: " << msg << std::endl;
    emit error(QString::fromStdString(msg));
}
