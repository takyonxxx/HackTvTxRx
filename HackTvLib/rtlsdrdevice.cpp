#include "rtlsdrdevice.h"
#include <iostream>

RTLSDRDevice::RTLSDRDevice(QObject *parent)
    : QObject(parent), device(nullptr), isRunning(false)
{

}

RTLSDRDevice::~RTLSDRDevice()
{
    if (device) {
        rtlsdr_close(device);
    }
}

void RTLSDRDevice::setDataCallback(DataCallback callback)
{
    m_dataCallback = std::move(callback);
}

bool RTLSDRDevice::initialize(uint32_t sampleRate, uint32_t frequency, int gain)
{
    int deviceCount = rtlsdr_get_device_count();
    std::cout << "RTL-SDR device count: " << deviceCount << std::endl;
    if (deviceCount == 0) {
        std::cout << "No RTL-SDR devices found" << std::endl;
        return false;
    }

    const char* deviceName = rtlsdr_get_device_name(0);
    std::cout << "Attempting to open device: " << (deviceName ? deviceName : "Unknown") << std::endl;

    int result = rtlsdr_open(&device, 0);  // Open the first available device
    if (result < 0) {
        std::cout << "Failed to open RTL-SDR device" << std::endl;
        return false;
    }

    rtlsdr_set_sample_rate(device, sampleRate);
    rtlsdr_set_center_freq(device, frequency);

    if (gain == 0) {
        rtlsdr_set_tuner_gain_mode(device, 0);  // Auto gain
    } else {
        rtlsdr_set_tuner_gain_mode(device, 1);  // Manual gain
        rtlsdr_set_tuner_gain(device, gain);
    }

    std::cout << "Successfully opened RTL-SDR device" << std::endl;

    return true;
}

void RTLSDRDevice::start()
{
    if (!device) {
        std::cout << "Device not initialized" << std::endl;
        return;
    }

    if (isRunning) {
        std::cout << "Device is already running" << std::endl;
        return;
    }

    isRunning = true;
    rtlsdr_reset_buffer(device);

    readThread = std::thread([this]() {
        rtlsdr_read_async(device, rtlsdrCallback, this, 0, 0);
    });
}

void RTLSDRDevice::stop()
{
    if (!isRunning) {
        std::cout << "Device is not running" << std::endl;
        return;
    }

    isRunning = false;
    rtlsdr_cancel_async(device);

    if (readThread.joinable()) {
        readThread.join();
    }
}

void RTLSDRDevice::setFrequency(uint64_t newFrequency)
{
    m_frequency = newFrequency;
    if (device) {
        int result = rtlsdr_set_center_freq(device, static_cast<uint32_t>(m_frequency));
        if (result < 0) {
            std::cerr << "Failed to set frequency to " << m_frequency << " Hz. Error code: " << result << std::endl;
        } else {
            std::cout << "Frequency set to " << m_frequency << " Hz" << std::endl;
        }
    } else {
        std::cerr << "Device not initialized. Frequency will be set on initialization." << std::endl;
    }
}

void RTLSDRDevice::setSampleRate(uint32_t newSampleRate)
{
    m_sampleRate = newSampleRate;
    if (device) {
        int result = rtlsdr_set_sample_rate(device, m_sampleRate);
        if (result < 0) {
            std::cerr << "Failed to set sample rate to " << m_sampleRate << " Hz. Error code: " << result << std::endl;
        } else {
            std::cout << "Sample rate set to " << m_sampleRate << " Hz" << std::endl;
        }
    } else {
        std::cerr << "Device not initialized. Sample rate will be set on initialization." << std::endl;
    }
}

void RTLSDRDevice::rtlsdrCallback(unsigned char *buf, uint32_t len, void *ctx)
{
    RTLSDRDevice *wrapper = static_cast<RTLSDRDevice*>(ctx);
    if (wrapper->m_dataCallback) {
        const int8_t* int8Buf = reinterpret_cast<const int8_t*>(buf);
        wrapper->m_dataCallback(int8Buf, len);
    }
}
