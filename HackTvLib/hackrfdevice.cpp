#include "hackrfdevice.h"
#include "modulation.h"
#include <iostream>
#include "constants.h"
#include <thread>
#include <chrono>
#include <unistd.h>
#include <QThread>

std::string removeZerosFromBeginning(const std::string &string) {
    size_t i = 0;
    while (i < string.length() && string[i] == '0') {
        i++;
    }
    return string.substr(i);
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
    m_isDestroying(false),
    amplitude(1.0f),
    filter_size(0.0f),
    modulation_index(5.0f),
    interpolation(48.0f),
    decimation(1)
{
    int r = hackrf_init();
    if(r != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_init() failed: %s (%d)\n",
                hackrf_error_name(static_cast<hackrf_error>(r)), r);
        throw std::runtime_error("Failed to initialize HackRF");
    }

    listDevices();
}

HackRfDevice::~HackRfDevice()
{
    // Önce destroying flag'i set et
    m_isDestroying.store(true);

    // Çalışan thread'leri güvenli şekilde durdur
    if (m_isRunning.load()) {
        m_isRunning.store(false);

        // Callback'lerin bitmesini bekle
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Device'ı güvenli şekilde kapat
    try {
        if (m_deviceMutex) {
            std::unique_lock<std::mutex> lock(*m_deviceMutex, std::try_to_lock);
            if (lock.owns_lock() && h_device) {
                // Streaming'i durdur
                if (hackrf_is_streaming(h_device) == HACKRF_TRUE) {
                    if (mode == RX) {
                        hackrf_stop_rx(h_device);
                    } else {
                        hackrf_stop_tx(h_device);
                    }

                    // Streaming'in durmasını bekle
                    int timeout = 30;
                    while (hackrf_is_streaming(h_device) == HACKRF_TRUE && timeout-- > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }

                hackrf_close(h_device);
                h_device = nullptr;
            }
        }
    } catch (...) {
        // Destructor'da exception fırlatma
    }

    // HackRF library'yi kapat
    if (!device_serials.empty()) {
        hackrf_exit();
    }
}

std::vector<std::string> HackRfDevice::listDevices()
{
    device_serials.clear();
    device_board_ids.clear();

    auto list = hackrf_device_list();
    if (!list) {
        std::cerr << "Cannot read HackRF devices list" << std::endl;
        return device_serials;
    }

    for (int i = 0; i < list->devicecount; ++i) {
        if (list->serial_numbers[i]) {
            device_serials.push_back(removeZerosFromBeginning(list->serial_numbers[i]));
            device_board_ids.push_back(list->usb_board_ids[i]);
            std::cout << "Found HackRF " << device_serials.back()
                      << " " << device_board_ids.back() << std::endl;
        }
    }

    hackrf_device_list_free(list);
    return device_serials;
}

int HackRfDevice::stop()
{
    // Zaten destroying durumundaysa çık
    if (m_isDestroying.load()) {
        return RF_OK;
    }

    // Mutex kontrolü
    if (!m_deviceMutex) {
        return RF_ERROR;
    }

    try {
        std::unique_lock<std::mutex> lock(*m_deviceMutex, std::try_to_lock);

        // Lock alınamazsa, biraz bekle ve tekrar dene
        if (!lock.owns_lock()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            lock.lock();
        }

        // Zaten durdurulmuşsa çık
        if (m_isStopped.load() || !h_device) {
            return RF_OK;
        }

        // Running flag'ini false yap
        m_isRunning.store(false);

        // Audio input'u durdur
        if (m_audioInput) {
            m_audioInput->stop();
            m_audioInput.reset();
        }

        int r = HACKRF_SUCCESS;

        // TX/RX'i durdur
        if (hackrf_is_streaming(h_device) == HACKRF_TRUE) {
            if (mode == RX) {
                r = hackrf_stop_rx(h_device);
            } else {
                r = hackrf_stop_tx(h_device);
            }

            if (r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_stop_%s() failed: %s (%d)\n",
                        (mode == RX ? "rx" : "tx"),
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
            }
        }

        // Lock'u bırak ve streaming'in durmasını bekle
        lock.unlock();

        // Streaming durması için bekle
        int timeout = 50; // 5 saniye timeout
        while (h_device && hackrf_is_streaming(h_device) == HACKRF_TRUE && timeout > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            timeout--;
        }

        // Tekrar lock al ve device'ı kapat
        lock.lock();

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
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in stop(): %s\n", e.what());
        return RF_ERROR;
    }
}

void HackRfDevice::reset()
{
    // Tüm durumları sıfırla
    m_isRunning.store(false);
    m_isStopped.store(true);
    mode = RX;

    // Audio input'u temizle
    if(m_audioInput) {
        m_audioInput->stop();
        m_audioInput.reset();
    }

    // Stream buffer'ları temizle
    stream_tx.free();

    // Modülasyon parametrelerini sıfırla
    amplitude = 1.0f;
    filter_size = 0.0f;
    modulation_index = 5.0f;
    interpolation = 48.0f;
    decimation = 1;
}

int HackRfDevice::start(rf_mode _mode)
{
    // Destroying kontrolü
    if (m_isDestroying.load()) {
        return RF_ERROR;
    }

    if (!m_deviceMutex) {
        return RF_ERROR;
    }

    try {
        std::lock_guard<std::mutex> lock(*m_deviceMutex);

        // Zaten çalışıyorsa hata döndür
        if (m_isRunning.load() || !m_isStopped.load()) {
            fprintf(stderr, "HackRF device is already running\n");
            return RF_ERROR;
        }

        mode = _mode;

        if (device_serials.empty()) {
            fprintf(stderr, "No HackRF devices found\n");
            return RF_ERROR;
        }

        // Device'ı aç
        if (!h_device) {
            int r = hackrf_open_by_serial(device_serials[0].c_str(), &h_device);
            if (r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_open() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
                return RF_ERROR;
            }
        }

        // Ayarları uygula
        if (!applySettings()) {
            fprintf(stderr, "Failed to apply HackRF settings\n");
            cleanup();
            return RF_ERROR;
        }

        std::cout << "HackRF Amp enabled: " << m_ampEnable << std::endl;

        // Running flag'ini set et (callback başlamadan önce)
        m_isStopped.store(false);
        m_isRunning.store(true);

        int r;
        if (mode == RX) {
            r = hackrf_start_rx(h_device, _rx_callback, this);
            if (r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_start_rx() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
                m_isRunning.store(false);
                m_isStopped.store(true);
                cleanup();
                return RF_ERROR;
            }
            printf("hackrf_start_rx() ok\n");
        }
        else if (mode == TX) {
            r = hackrf_start_tx(h_device, _tx_callback, this);
            if (r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_start_tx() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);              
                m_isRunning.store(false);
                m_isStopped.store(true);
                cleanup();
                return RF_ERROR;
            }
            printf("hackrf_start_tx() ok\n");
        }
        else {
            fprintf(stderr, "Invalid mode specified\n");
            m_isRunning.store(false);
            m_isStopped.store(true);
            cleanup();
            return RF_ERROR;
        }

        std::cout << "HackRF Started" << std::endl;
        return RF_OK;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in start(): %s\n", e.what());
        return RF_ERROR;
    }
}

bool HackRfDevice::applySettings()
{
    if (!h_device) return false;

    struct Setting {
        std::function<int()> apply;
        const char* name;
    };

    std::vector<Setting> settings = {
        {[this]() { return hackrf_set_freq(h_device, m_frequency); }, "frequency"},
        {[this]() { return hackrf_set_sample_rate(h_device, m_sampleRate); }, "sample_rate"},
        {[this]() { return hackrf_set_lna_gain(h_device, m_lnaGain); }, "lna_gain"},
        {[this]() { return hackrf_set_vga_gain(h_device, m_vgaGain); }, "vga_gain"},
        {[this]() { return hackrf_set_txvga_gain(h_device, m_txAmpGain); }, "txvga_gain"},
        {[this]() { return hackrf_set_amp_enable(h_device, m_ampEnable ? 1 : 0); }, "amp_enable"},
        {[this]() { return hackrf_set_baseband_filter_bandwidth(h_device,
                                                                hackrf_compute_baseband_filter_bw(m_sampleRate)); }, "filter_bandwidth"},
        {[this]() { return hackrf_set_antenna_enable(h_device, m_antennaEnable ? 1 : 0); }, "antenna_enable"}
    };

    for (const auto& setting : settings) {
        int r = setting.apply();
        if (r != HACKRF_SUCCESS) {
            fprintf(stderr, "Failed to set %s: %s (%d)\n",
                    setting.name, hackrf_error_name(static_cast<hackrf_error>(r)), r);
            return false;
        }
    }

    return true;
}

void HackRfDevice::cleanup()
{
    if (h_device) {
        if (hackrf_is_streaming(h_device) == HACKRF_TRUE) {
            if (mode == RX) {
                hackrf_stop_rx(h_device);
            } else {
                hackrf_stop_tx(h_device);
            }
        }
        hackrf_close(h_device);
        h_device = nullptr;
    }
}

std::vector<float> HackRfDevice::readStreamToSize(size_t size)
{
    std::vector<float> float_buffer;
    float_buffer.reserve(size);

    while (float_buffer.size() < size && m_isRunning.load()) {
        std::vector<float> temp_buffer = stream_tx.readBufferToVector();

        if (temp_buffer.empty()) {
            // Stream boşsa biraz bekle
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        size_t elements_needed = size - float_buffer.size();
        size_t elements_to_add = std::min(elements_needed, temp_buffer.size());

        float_buffer.insert(float_buffer.end(),
                            temp_buffer.begin(),
                            temp_buffer.begin() + elements_to_add);
    }

    return float_buffer;
}

int HackRfDevice::_tx_callback(hackrf_transfer *transfer)
{
    HackRfDevice *device = static_cast<HackRfDevice*>(transfer->tx_ctx);

    if (!device || device->m_isDestroying.load() || !device->m_isRunning.load()) {
        return -1;
    }

    try {
        return device->apply_fm_modulation(
            reinterpret_cast<int8_t*>(transfer->buffer),
            transfer->valid_length
            );
    }
    catch (...) {
        return -1;
    }
}

int HackRfDevice::_rx_callback(hackrf_transfer *transfer)
{
    HackRfDevice *device = static_cast<HackRfDevice*>(transfer->rx_ctx);

    if (!device || device->m_isDestroying.load() || !device->m_isRunning.load()) {
        return -1;
    }

    try {
        auto rf_data = reinterpret_cast<int8_t*>(transfer->buffer);
        auto len = transfer->valid_length;

        if (len % 2 != 0) {
            return -1;
        }

        if (device->m_dataCallback) {
            device->m_dataCallback(rf_data, len);
        }

        return 0;
    }
    catch (...) {
        return -1;
    }
}

int HackRfDevice::apply_fm_modulation(int8_t* buffer, uint32_t length)
{
    if (!m_isRunning.load() || !buffer || length == 0) {
        return -1;
    }

    try {
        size_t desired_size = length / 2;
        std::vector<float> float_buffer = readStreamToSize(desired_size);

        if (float_buffer.size() < desired_size) {
            // Buffer'ı sıfırla
            std::memset(buffer, 0, length);
            return 0;
        }

        // Amplitude uygula
        for (auto& sample : float_buffer) {
            sample *= amplitude;
        }

        // FM modülasyonu uygula
        std::vector<std::complex<float>> modulated_signal(float_buffer.size());
        FrequencyModulator modulator(modulation_index);
        modulator.work(float_buffer.size(), float_buffer, modulated_signal);

        // Resampling
        RationalResampler resampler(interpolation, decimation, filter_size);
        std::vector<std::complex<float>> resampled_signal = resampler.resample(modulated_signal);

        // Buffer boyutu kontrolü
        size_t output_samples = std::min(static_cast<size_t>(length / 2), resampled_signal.size());

        // IQ verilerini buffer'a yaz
        for (size_t i = 0; i < output_samples; ++i) {
            buffer[2 * i] = static_cast<int8_t>(
                std::max(-127.0f, std::min(127.0f, std::real(resampled_signal[i]) * 127.0f))
                );
            buffer[2 * i + 1] = static_cast<int8_t>(
                std::max(-127.0f, std::min(127.0f, std::imag(resampled_signal[i]) * 127.0f))
                );
        }

        // Kalan buffer'ı sıfırla
        if (output_samples * 2 < length) {
            std::memset(buffer + output_samples * 2, 0, length - output_samples * 2);
        }

        return 0;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in apply_fm_modulation: %s\n", e.what());
        return -1;
    }
}

// Thread-safe setter implementasyonları
void HackRfDevice::setFrequency(uint64_t frequency_hz)
{
    if (m_deviceMutex) {
        std::lock_guard<std::mutex> lock(*m_deviceMutex);
        m_frequency = frequency_hz;
        if (h_device && !m_isDestroying.load()) {
            hackrf_set_freq(h_device, m_frequency);
        }
    }
}

void HackRfDevice::setSampleRate(uint32_t sample_rate)
{
    if (m_deviceMutex) {
        std::lock_guard<std::mutex> lock(*m_deviceMutex);
        m_sampleRate = sample_rate;
        if (h_device && !m_isDestroying.load()) {
            hackrf_set_sample_rate(h_device, m_sampleRate);
            hackrf_set_baseband_filter_bandwidth(h_device,
                                                 hackrf_compute_baseband_filter_bw(m_sampleRate));
        }
    }
}

// Diğer setter'lar da benzer şekilde thread-safe yapılmalı...

void HackRfDevice::setInterpolation(float newInterpolation)
{
    interpolation.store(newInterpolation);
}

void HackRfDevice::setMicEnabled(bool enable)
{
    if(!m_audioInput && enable)
    {
        m_audioInput = std::make_unique<PortAudioInput>(stream_tx);
        if (!m_audioInput->start()) {
            std::cerr << "Failed to start PortAudioInput" << std::endl;
        }
    }
    else if (m_audioInput && !enable) {
        m_audioInput->stop();
        m_audioInput.reset();
    }
}

void HackRfDevice::setDecimation(int newDecimation)
{
    decimation.store(newDecimation);
}

void HackRfDevice::setModulation_index(float newModulation_index)
{
    modulation_index.store(newModulation_index);
}

void HackRfDevice::setFilter_size(float newFilter_size)
{
    filter_size.store(newFilter_size);
}

void HackRfDevice::setAmplitude(float newAmplitude)
{
    amplitude.store(newAmplitude);
}

void HackRfDevice::setLnaGain(unsigned int lna_gain)
{
    m_lnaGain = lna_gain;
    if (h_device && !m_isDestroying.load()) {
        std::lock_guard<std::mutex> lock(*m_deviceMutex);
        hackrf_set_lna_gain(h_device, m_lnaGain);
    }
}

void HackRfDevice::setVgaGain(unsigned int vga_gain)
{
    m_vgaGain = vga_gain;
    if (h_device && !m_isDestroying.load()) {
        std::lock_guard<std::mutex> lock(*m_deviceMutex);
        hackrf_set_vga_gain(h_device, m_vgaGain);
    }
}

void HackRfDevice::setTxAmpGain(unsigned int tx_amp_gain)
{
    m_txAmpGain = tx_amp_gain;
    if (h_device && !m_isDestroying.load()) {
        std::lock_guard<std::mutex> lock(*m_deviceMutex);
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
    if (h_device && !m_isDestroying.load()) {
        std::lock_guard<std::mutex> lock(*m_deviceMutex);
        hackrf_set_amp_enable(h_device, m_ampEnable ? 1 : 0);
    }
}

void HackRfDevice::setDataCallback(DataCallback callback)
{
    m_dataCallback = callback;
}
