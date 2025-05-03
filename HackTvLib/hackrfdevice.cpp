
#include "hackrfdevice.h"
#include "modulation.h"
#include <iostream>
#include "constants.h"
#include <thread>
#include <chrono>
#include <unistd.h>
#include <QThread>

std::string removeZerosFromBeginning(const std::string &string) {
    uint32_t i = 0;
    while (i < string.length() && string[i] == '0') {
        i++;
    }
    return string.substr(i, string.length() - i);
}

HackRfDevice::HackRfDevice(QObject *parent):
    QObject(parent),
    h_device(nullptr),
    m_deviceMutex(std::make_shared<std::mutex>()),
    m_frequency(DEFAULT_FREQUENCY),
    m_sampleRate(DEFAULT_SAMPLE_RATE),
    m_lnaGain(HACKRF_RX_LNA_MAX_DB),
    m_vgaGain(HACKRF_RX_VGA_MAX_DB),
    m_txAmpGain(HACKRF_TX_AMP_MAX_DB),
    m_rxAmpGain(HACKRF_RX_AMP_MAX_DB),
    m_ampEnable(false),
    m_antennaEnable(false),
    m_isStopped(true),
    m_isRunning(false),
    m_isDestroying(false)
{
    int r = hackrf_init();
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_init() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        return;
    }

    listDevices();
}

HackRfDevice::~HackRfDevice()
{
    m_isDestroying.store(true);

    // Stop device if running
    if (!m_isStopped.load()) {
        stop();
    }

    // Wait a bit to ensure all callbacks are finished
    QThread::msleep(200);

    // Now it's safe to call hackrf_exit
    if (!device_serials.empty()) {
        hackrf_exit();
    }
}

std::vector<std::string> HackRfDevice::listDevices()
{
    auto list = hackrf_device_list();
    if (!list) {
        std::cout << "Cannot read HackRF devices list" << std::endl;
        return device_serials;
    }
    for (int i = 0; i < list->devicecount; ++i) {
        if (!list->serial_numbers[i]) {
            std::cout << "Cannot read HackRF serial" << std::endl;
            continue;
        }
        device_serials.push_back(removeZerosFromBeginning(list->serial_numbers[i]));
        device_board_ids.push_back(list->usb_board_ids[i]);
        std::cout << "Found HackRF " << device_serials.back() << " " << device_board_ids.back() << std::endl;
    }
    hackrf_device_list_free(list);
    return device_serials;
}

int HackRfDevice::stop()
{
    // Check if object is being destroyed
    if (m_isDestroying.load()) {
        return RF_OK;
    }

    // Check if mutex is still valid
    if (!m_deviceMutex) {
        return RF_ERROR;
    }

    // Use try-catch to handle potential mutex errors
    try {
        std::unique_lock<std::mutex> lock(*m_deviceMutex);

        // Check if already stopped
        if (m_isStopped.load() || !h_device) {
            return RF_OK;
        }

        // Mark as stopping to prevent new operations
        m_isRunning.store(false);

        int r;

        // Stop TX/RX
        if (mode == RX) {
            r = hackrf_stop_rx(h_device);
        } else {
            if (m_audioInput) {
                m_audioInput->stop();
                m_audioInput.reset();
            }
            r = hackrf_stop_tx(h_device);
        }

        if (r != HACKRF_SUCCESS) {
            fprintf(stderr, "hackrf_stop_%s() failed: %s (%d)\n",
                    (mode == RX ? "rx" : "tx"),
                    hackrf_error_name(static_cast<hackrf_error>(r)), r);
        }

        // Unlock mutex before waiting for streaming to stop
        lock.unlock();

        // Wait for streaming to stop (without holding the mutex)
        int timeout = 50;  // 5 seconds timeout
        while (h_device && hackrf_is_streaming(h_device) == HACKRF_TRUE && timeout > 0) {
            QThread::msleep(100);
            timeout--;
        }

        // Re-lock for final cleanup if not destroying
        if (!m_isDestroying.load()) {
            lock.lock();
        } else {
            return RF_OK;
        }

        if (timeout == 0) {
            fprintf(stderr, "Warning: HackRF streaming didn't stop after timeout\n");
        }

        // Close the device
        if (h_device) {
            r = hackrf_close(h_device);
            if (r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_close() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
            }
            h_device = nullptr;
        }

        m_isStopped.store(true);

        std::cout << "HackRF Stopped" << std::endl;
        return RF_OK;
    }
    catch (const std::system_error& e) {
        fprintf(stderr, "Mutex error in stop(): %s\n", e.what());
        return RF_ERROR;
    }
}

int HackRfDevice::start(rf_mode _mode)
{
    // Check if object is being destroyed
    if (m_isDestroying.load()) {
        return RF_ERROR;
    }

    if (!m_deviceMutex) {
        return RF_ERROR;
    }

    try {
        std::lock_guard<std::mutex> lock(*m_deviceMutex);

        // Check if already running
        if (m_isRunning.load()) {
            fprintf(stderr, "HackRF device is already running\n");
            return RF_ERROR;
        }

        mode = _mode;

        if (device_serials.empty()) {
            fprintf(stderr, "No HackRF devices found\n");
            return RF_ERROR;
        }

        // Open device if not already open
        if (!h_device) {
            int r = hackrf_open_by_serial(device_serials[0].c_str(), &h_device);
            if (r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_open() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
                return RF_ERROR;
            }
        }

        // Apply all settings
        if (!applySettings()) {
            fprintf(stderr, "Failed to apply HackRF settings\n");
            cleanup();
            return RF_ERROR;
        }

        std::cout << "HackRF Amp enabled : " << m_ampEnable << std::endl;

        int r;
        if (mode == RX) {
            r = hackrf_start_rx(h_device, _rx_callback, this);
            if (r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_start_rx() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
                cleanup();
                return RF_ERROR;
            }
            printf("hackrf_start_rx() ok\n");
        }
        else if (mode == TX) {
            m_audioInput = std::make_unique<PortAudioInput>(stream_tx);
            if (!m_audioInput->start()) {
                std::cerr << "Failed to start PortAudioInput" << std::endl;
                cleanup();
                return RF_ERROR;
            }

            r = hackrf_start_tx(h_device, _tx_callback, this);
            if (r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_start_tx() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
                m_audioInput->stop();
                cleanup();
                return RF_ERROR;
            }
            printf("hackrf_start_tx() ok\n");
        }
        else {
            fprintf(stderr, "Invalid mode specified\n");
            cleanup();
            return RF_ERROR;
        }

        m_isStopped.store(false);
        m_isRunning.store(true);
        std::cout << "HackRF Started" << std::endl;
        return RF_OK;
    }
    catch (const std::system_error& e) {
        fprintf(stderr, "Mutex error in start(): %s\n", e.what());
        return RF_ERROR;
    }
}

bool HackRfDevice::applySettings()
{
    if (!h_device) return false;

    int r;

    // Apply all settings and check for errors
    r = hackrf_set_freq(h_device, m_frequency);
    if (r != HACKRF_SUCCESS) return false;

    r = hackrf_set_sample_rate(h_device, m_sampleRate);
    if (r != HACKRF_SUCCESS) return false;

    r = hackrf_set_lna_gain(h_device, m_lnaGain);
    if (r != HACKRF_SUCCESS) return false;

    r = hackrf_set_vga_gain(h_device, m_vgaGain);
    if (r != HACKRF_SUCCESS) return false;

    r = hackrf_set_txvga_gain(h_device, m_txAmpGain);
    if (r != HACKRF_SUCCESS) return false;

    r = hackrf_set_amp_enable(h_device, m_ampEnable ? 1 : 0);
    if (r != HACKRF_SUCCESS) return false;

    r = hackrf_set_baseband_filter_bandwidth(h_device,
                                             hackrf_compute_baseband_filter_bw(m_sampleRate));
    if (r != HACKRF_SUCCESS) return false;

    r = hackrf_set_antenna_enable(h_device, m_antennaEnable ? 1 : 0);
    if (r != HACKRF_SUCCESS) return false;

    return true;
}

void HackRfDevice::cleanup()
{
    if (h_device) {
        hackrf_close(h_device);
        h_device = nullptr;
    }
}

std::vector<float> HackRfDevice::readStreamToSize(size_t size) {
    std::vector<float> float_buffer;
    float_buffer.reserve(size);
    while (float_buffer.size() < size) {
        std::vector<float> temp_buffer = stream_tx.readBufferToVector();
        size_t elements_needed = size - float_buffer.size();
        size_t elements_to_add = (elements_needed < temp_buffer.size()) ? elements_needed : temp_buffer.size();
        float_buffer.insert(float_buffer.end(), temp_buffer.begin(), temp_buffer.begin() + elements_to_add);
    }
    return float_buffer;
}

int HackRfDevice::_tx_callback(hackrf_transfer *transfer)
{
    HackRfDevice *device = reinterpret_cast<HackRfDevice *>(transfer->tx_ctx);

    // Check if device is still valid and running
    if (!device || !device->m_isRunning.load() || device->m_isDestroying.load()) {
        return -1;  // Stop the callback
    }

    return device->apply_fm_modulation((int8_t *)transfer->buffer, transfer->valid_length);
}

int HackRfDevice::_rx_callback(hackrf_transfer *transfer)
{
    HackRfDevice *device = reinterpret_cast<HackRfDevice *>(transfer->rx_ctx);

    // Check if device is still valid and running
    if (!device || !device->m_isRunning.load() || device->m_isDestroying.load()) {
        return -1;  // Stop the callback
    }

    auto rf_data = reinterpret_cast<int8_t*>(transfer->buffer);
    auto len = transfer->valid_length;

    if (len % 2 != 0) {
        return -1; // Invalid data length
    }

    if (device->m_dataCallback) {
        device->m_dataCallback(rf_data, len);
    }

    return 0;
}

void HackRfDevice::setInterpolation(float newInterpolation)
{
    interpolation = newInterpolation;
}

int HackRfDevice::apply_fm_modulation(int8_t* buffer, uint32_t length)
{
    // Check if still running
    if (!m_isRunning.load()) {
        return -1;
    }

    size_t desired_size = length / 2;
    std::vector<float> float_buffer = readStreamToSize(desired_size);

    if (float_buffer.size() < desired_size) {
        return 0;
    }

    int noutput_items = float_buffer.size();
    for (int i = 0; i < noutput_items; ++i) {
        float_buffer[i] *= amplitude;
    }

    std::vector<std::complex<float>> modulated_signal(noutput_items);
    float sensitivity = modulation_index;
    FrequencyModulator modulator(sensitivity);
    modulator.work(noutput_items, float_buffer, modulated_signal);

    RationalResampler resampler(interpolation, decimation, filter_size);
    std::vector<std::complex<float>> resampled_signal = resampler.resample(modulated_signal);

    for (int i = 0; i < noutput_items; ++i) {
        buffer[2 * i] = static_cast<int8_t>(std::real(resampled_signal[i]) * 127.0f);
        buffer[2 * i + 1] = static_cast<int8_t>(std::imag(resampled_signal[i]) * 127.0f);
    }

    return 0;
}

void HackRfDevice::setDecimation(int newDecimation)
{
    decimation = newDecimation;
}

void HackRfDevice::setModulation_index(float newModulation_index)
{
    modulation_index = newModulation_index;
}

void HackRfDevice::setFilter_size(float newFilter_size)
{
    filter_size = newFilter_size;
}

void HackRfDevice::setAmplitude(float newAmplitude)
{
    amplitude = newAmplitude;
}

void HackRfDevice::setDataCallback(DataCallback callback)
{
    m_dataCallback = std::move(callback);
}

void HackRfDevice::setFrequency(uint64_t frequency_hz)
{
    m_frequency = frequency_hz;
    if (h_device) {
        hackrf_set_freq(h_device, m_frequency);
    }
}

void HackRfDevice::setSampleRate(uint32_t sample_rate)
{
    m_sampleRate = sample_rate;
    if (h_device) {
        hackrf_set_sample_rate(h_device, m_sampleRate);
        setBasebandFilterBandwidth(hackrf_compute_baseband_filter_bw(m_sampleRate));
    }
}

void HackRfDevice::setLnaGain(unsigned int lna_gain)
{
    m_lnaGain = lna_gain;
    if (h_device) {
        hackrf_set_lna_gain(h_device, m_lnaGain);
    }
}

void HackRfDevice::setVgaGain(unsigned int vga_gain)
{
    m_vgaGain = vga_gain;
    if (h_device) {
        hackrf_set_vga_gain(h_device, m_vgaGain);
    }
}

void HackRfDevice::setTxAmpGain(unsigned int tx_amp_gain)
{
    m_txAmpGain = tx_amp_gain;
    if (h_device) {
        hackrf_set_txvga_gain(h_device, m_txAmpGain);
    }
}

void HackRfDevice::setRxAmpGain(unsigned int rx_amp_gain)
{
    m_rxAmpGain = rx_amp_gain;
}

void HackRfDevice::setAmpEnable(bool enable)
{
    m_ampEnable = enable;
    if (h_device) {
        hackrf_set_amp_enable(h_device, m_ampEnable ? 1 : 0);
    }
}

void HackRfDevice::setBasebandFilterBandwidth(uint32_t bandwidth)
{
    m_basebandFilterBandwidth = bandwidth;
    if (h_device) {
        hackrf_set_baseband_filter_bandwidth(h_device, m_basebandFilterBandwidth);
    }
}

void HackRfDevice::setAntennaEnable(bool enable)
{
    m_antennaEnable = enable;
    if (h_device) {
        hackrf_set_antenna_enable(h_device, m_antennaEnable ? 1 : 0);
    }
}

// Getter implementations
uint64_t HackRfDevice::getFrequency() const
{
    return m_frequency;
}

uint32_t HackRfDevice::getSampleRate() const
{
    return m_sampleRate;
}

unsigned int HackRfDevice::getLnaGain() const
{
    return m_lnaGain;
}

unsigned int HackRfDevice::getVgaGain() const
{
    return m_vgaGain;
}

unsigned int HackRfDevice::getTxAmpGain() const
{
    return m_txAmpGain;
}

bool HackRfDevice::getAmpEnable() const
{
    return m_ampEnable;
}

uint32_t HackRfDevice::getBasebandFilterBandwidth() const
{
    return m_basebandFilterBandwidth;
}

bool HackRfDevice::getAntennaEnable() const
{
    return m_antennaEnable;
}
