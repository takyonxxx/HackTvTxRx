#include "hackrfdevice.h"
#include "audiofileinput.h"
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
    // Initialize audio ring buffer
    m_audioRing.resize(AUDIO_RING_SIZE, 0.0f);

    // hackrf_init() must only be called ONCE per process lifetime.
    // Multiple init/exit cycles cause USB handle corruption and crashes.
    static bool s_hackrf_initialized = false;
    static bool s_hackrf_init_failed = false;

    if (s_hackrf_init_failed) {
        fprintf(stderr, "hackrf_init() previously failed - skipping\n");
        fflush(stderr);
        return; // Don't throw - let caller check isReady()
    }

    if (!s_hackrf_initialized) {
        fprintf(stderr, "Calling hackrf_init()...\n");
        fflush(stderr);
        try {
            int r = hackrf_init();
            if(r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_init() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
                fflush(stderr);
                s_hackrf_init_failed = true;
                return; // Don't throw
            }
            s_hackrf_initialized = true;
            fprintf(stderr, "hackrf_init() OK (one-time)\n");
            fflush(stderr);
        } catch (...) {
            fprintf(stderr, "Exception in hackrf_init()\n");
            fflush(stderr);
            s_hackrf_init_failed = true;
            return;
        }
    }

    try {
        listDevices();
    } catch (...) {
        fprintf(stderr, "Exception in listDevices()\n");
        fflush(stderr);
    }
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

    // Do NOT call hackrf_exit() here - it must only be called once at process exit.
    // Calling hackrf_exit() + hackrf_init() on re-create causes USB handle corruption.
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
        if (m_audioFileInput) {
            m_audioFileInput->stop();
            delete m_audioFileInput; m_audioFileInput = nullptr;
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
    if(m_audioFileInput) {
        m_audioFileInput->stop();
        delete m_audioFileInput; m_audioFileInput = nullptr;
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

int HackRfDevice::hardReset()
{
    // USB hard reset: hackrf_reset() causes the device to detach and re-attach
    // on the USB bus (you will hear the USB plug/unplug sound on Windows).
    // This is equivalent to physically unplugging and re-plugging the device.

    fprintf(stderr, "=== HackRfDevice::hardReset() - USB DEVICE RESET ===\n");
    fflush(stderr);

    if (!m_deviceMutex) {
        fprintf(stderr, "hardReset: mutex is null\n");
        fflush(stderr);
        return RF_ERROR;
    }

    try {
        std::lock_guard<std::mutex> lock(*m_deviceMutex);

        // Stop streaming first if running
        if (m_isRunning.load() && h_device) {
            m_isRunning.store(false);

            if (m_audioInput) {
                m_audioInput->stop();
                m_audioInput.reset();
            }
            if (m_audioFileInput) {
                m_audioFileInput->stop();
                delete m_audioFileInput; m_audioFileInput = nullptr;
            }

            if (hackrf_is_streaming(h_device) == HACKRF_TRUE) {
                if (mode == RX) {
                    hackrf_stop_rx(h_device);
                } else {
                    hackrf_stop_tx(h_device);
                }
                // Wait for streaming to actually stop
                int timeout = 30;
                while (hackrf_is_streaming(h_device) == HACKRF_TRUE && timeout-- > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }

        // If we don't have a device handle, try to open one just for reset
        bool need_open = (h_device == nullptr);
        if (need_open) {
            fprintf(stderr, "hardReset: no device handle, opening for reset...\n");
            fflush(stderr);

            listDevices();
            if (device_serials.empty()) {
                fprintf(stderr, "hardReset: no HackRF devices found\n");
                fflush(stderr);
                return RF_ERROR;
            }

            int r = hackrf_open_by_serial(device_serials[0].c_str(), &h_device);
            if (r != HACKRF_SUCCESS || !h_device) {
                fprintf(stderr, "hardReset: hackrf_open() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
                fflush(stderr);
                h_device = nullptr;
                return RF_ERROR;
            }
        }

        // === THE ACTUAL USB HARD RESET ===
        fprintf(stderr, "hardReset: calling hackrf_reset() - USB detach/re-attach...\n");
        fflush(stderr);

        int r = hackrf_reset(h_device);

        // After hackrf_reset(), the device handle is INVALID regardless of return code.
        // The device has detached from USB and will re-enumerate.
        h_device = nullptr;
        m_isRunning.store(false);
        m_isStopped.store(true);

        if (r != HACKRF_SUCCESS) {
            fprintf(stderr, "hardReset: hackrf_reset() returned: %s (%d) - device may still have reset\n",
                    hackrf_error_name(static_cast<hackrf_error>(r)), r);
            fflush(stderr);
        } else {
            fprintf(stderr, "hardReset: hackrf_reset() OK - device is re-enumerating on USB bus\n");
            fflush(stderr);
        }

        // Clear internal state
        stream_tx.free();
        device_serials.clear();
        device_board_ids.clear();

        fprintf(stderr, "=== hardReset complete - wait for USB re-enumeration ===\n");
        fflush(stderr);

        return RF_OK;

    } catch (const std::exception& e) {
        fprintf(stderr, "Exception in hardReset(): %s\n", e.what());
        fflush(stderr);
        h_device = nullptr;
        m_isRunning.store(false);
        m_isStopped.store(true);
        return RF_ERROR;
    }
}

int HackRfDevice::start(rf_mode _mode)
{
    fprintf(stderr, "HackRfDevice::start() ENTRY, mode=%d\n", _mode);
    fflush(stderr);

    // Destroying kontrolü
    if (m_isDestroying.load()) {
        fprintf(stderr, "Device is being destroyed, cannot start\n");
        fflush(stderr);
        return RF_ERROR;
    }

    if (!m_deviceMutex) {
        fprintf(stderr, "Device mutex is null\n");
        fflush(stderr);
        return RF_ERROR;
    }

    try {
        std::lock_guard<std::mutex> lock(*m_deviceMutex);

        // Zaten çalışıyorsa hata döndür
        if (m_isRunning.load() || !m_isStopped.load()) {
            fprintf(stderr, "HackRF device is already running\n");
            fflush(stderr);
            return RF_ERROR;
        }

        mode = _mode;

        // Force close any stale device handle before opening
        if (h_device) {
            fprintf(stderr, "Closing stale HackRF handle before re-open...\n");
            fflush(stderr);
            hackrf_stop_rx(h_device);
            hackrf_stop_tx(h_device);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            hackrf_close(h_device);
            h_device = nullptr;
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }

        // Re-enumerate devices in case USB state changed
        listDevices();

        if (device_serials.empty()) {
            fprintf(stderr, "No HackRF devices found\n");
            fflush(stderr);
            return RF_ERROR;
        }

        // Open device
        {
            fprintf(stderr, "Opening HackRF device: %s\n", device_serials[0].c_str());
            fflush(stderr);

            int r = hackrf_open_by_serial(device_serials[0].c_str(), &h_device);
            if (r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_open() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
                fflush(stderr);
                return RF_ERROR;
            }

            fprintf(stderr, "HackRF device opened successfully\n");
            fflush(stderr);
        }

        // Ayarları uygula
        fprintf(stderr, "Applying settings...\n");
        fflush(stderr);

        if (!applySettings()) {
            fprintf(stderr, "Failed to apply HackRF settings\n");
            fflush(stderr);
            cleanup();
            return RF_ERROR;
        }

        fprintf(stderr, "Settings applied successfully\n");
        fflush(stderr);

        fprintf(stderr, "HackRF Amp enabled: %d\n", m_ampEnable);
        fflush(stderr);

        // Running flag'ini set et (callback başlamadan önce)
        m_isStopped.store(false);
        m_isRunning.store(true);

        int r;
        if (mode == RX) {
            fprintf(stderr, "Starting RX mode...\n");
            fflush(stderr);

            r = hackrf_start_rx(h_device, _rx_callback, this);
            if (r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_start_rx() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
                fflush(stderr);
                m_isRunning.store(false);
                m_isStopped.store(true);
                cleanup();
                return RF_ERROR;
            }
            fprintf(stderr, "hackrf_start_rx() ok\n");
            fflush(stderr);
        }
        else if (mode == TX) {
            fprintf(stderr, "Starting TX mode...\n");
            fflush(stderr);

            r = hackrf_start_tx(h_device, _tx_callback, this);
            if (r != HACKRF_SUCCESS) {
                fprintf(stderr, "hackrf_start_tx() failed: %s (%d)\n",
                        hackrf_error_name(static_cast<hackrf_error>(r)), r);
                fflush(stderr);
                m_isRunning.store(false);
                m_isStopped.store(true);
                cleanup();
                return RF_ERROR;
            }
            fprintf(stderr, "hackrf_start_tx() ok\n");
            fflush(stderr);
        }
        else {
            fprintf(stderr, "Invalid mode specified: %d\n", mode);
            fflush(stderr);
            m_isRunning.store(false);
            m_isStopped.store(true);
            cleanup();
            return RF_ERROR;
        }

        fprintf(stderr, "HackRF Started successfully\n");
        fflush(stderr);
        return RF_OK;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in start(): %s\n", e.what());
        fflush(stderr);
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

std::vector<float> HackRfDevice::readStreamToSize(size_t size)
{
    std::vector<float> float_buffer;
    float_buffer.reserve(size);

    // CRITICAL: Add timeout to prevent infinite blocking
    const int MAX_WAIT_MS = 100; // 100ms timeout
    const auto start_time = std::chrono::steady_clock::now();

    while (float_buffer.size() < size && m_isRunning.load()) {
        // Check timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start_time
                           ).count();

        if (elapsed > MAX_WAIT_MS) {
            // Timeout - return what we have (may be empty)
            break;
        }

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

int HackRfDevice::apply_fm_modulation(int8_t* buffer, uint32_t length)
{
    if (!m_isRunning.load() || !buffer || length == 0) {
        return -1;
    }

    try {
        size_t output_iq_needed = length / 2;
        std::vector<float> float_buffer;

        if (m_useAudioFileRing.load()) {
            // Ring buffer path (both mic and file)
            float interp = interpolation.load();
            int decim = decimation.load();
            float ratio = (interp > 0.0f) ? (interp / static_cast<float>(std::max(decim, 1))) : 48.0f;
            size_t audio_needed = static_cast<size_t>(output_iq_needed / ratio) + 16;

            float_buffer.resize(audio_needed, 0.0f);
            size_t got = ringRead(float_buffer.data(), audio_needed);

            // Need minimum samples for FM modulator + resampler to work properly
            if (got < 64) {
                std::memset(buffer, 0, length);
                return 0;
            }
            float_buffer.resize(got);
        } else {
            // Mic mode: read from stream_tx (original unchanged path)
            size_t desired_size = output_iq_needed;
            if (stream_tx.isEmpty()) {
                std::memset(buffer, 0, length);
                return 0;
            }

            float_buffer = readStreamToSize(desired_size);

            if (float_buffer.empty() || float_buffer.size() < desired_size) {
                std::memset(buffer, 0, length);
                return 0;
            }
        }

        // Amplitude uygula
        for (auto& sample : float_buffer) {
            sample *= amplitude.load();
        }

        // FM modulation + resampling with persistent state across callbacks.
        // Recreate only when parameters change (user adjusts GUI sliders).
        static FrequencyModulator* s_modulator = nullptr;
        static RationalResampler* s_resampler = nullptr;
        static float s_lastModIndex = -1.0f;
        static float s_lastFilterSize = -1.0f;
        static float s_lastInterp = -1.0f;
        static int s_lastDecim = -1;

        float curModIndex = modulation_index.load();
        float curFilterSize = filter_size.load();
        float curInterp = interpolation.load();
        int curDecim = std::max(decimation.load(), 1);

        if (!s_modulator || curModIndex != s_lastModIndex) {
            delete s_modulator;
            s_modulator = new FrequencyModulator(curModIndex);
            s_lastModIndex = curModIndex;
        }

        if (!s_resampler || curFilterSize != s_lastFilterSize ||
            curInterp != s_lastInterp || curDecim != s_lastDecim) {
            delete s_resampler;
            s_resampler = new RationalResampler(curInterp, curDecim, curFilterSize);
            s_lastFilterSize = curFilterSize;
            s_lastInterp = curInterp;
            s_lastDecim = curDecim;
        }

        std::vector<std::complex<float>> modulated_signal(float_buffer.size());
        s_modulator->work(float_buffer.size(), float_buffer, modulated_signal);

        // Resampling with persistent filter + interpolation state
        std::vector<std::complex<float>> resampled_signal = s_resampler->resample(modulated_signal);

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
        fflush(stderr);
        // On error, send silence (don't stop the stream)
        std::memset(buffer, 0, length);
        return 0;
    }
    catch (...) {
        fprintf(stderr, "Unknown exception in apply_fm_modulation\n");
        fflush(stderr);
        // On error, send silence
        std::memset(buffer, 0, length);
        return 0;
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
        // Stop file input if running
        if (m_audioFileInput) {
            m_audioFileInput->stop();
            delete m_audioFileInput; m_audioFileInput = nullptr;
        }

        // Reset and enable ring buffer for mic
        ringReset();
        m_useAudioFileRing.store(true); // Same ring buffer path for both mic and file

        m_audioInput = std::make_unique<PortAudioInput>(*this);
        if (!m_audioInput->start()) {
            std::cerr << "Failed to start PortAudioInput" << std::endl;
            m_useAudioFileRing.store(false);
        }
    }
    else if (m_audioInput && !enable) {
        m_audioInput->stop();
        m_audioInput.reset();
        m_useAudioFileRing.store(false);
    }
}

void HackRfDevice::setAudioFileEnabled(bool enable, const std::string& filePath, bool loop)
{
    if (enable && !filePath.empty())
    {
        // Stop mic if running
        if (m_audioInput) {
            m_audioInput->stop();
            m_audioInput.reset();
        }
        // Stop previous file playback if running
        if (m_audioFileInput) {
            m_audioFileInput->stop();
            delete m_audioFileInput; m_audioFileInput = nullptr;
        }

        // Reset and enable ring buffer
        ringReset();
        m_useAudioFileRing.store(true);

        m_audioFileInput = new AudioFileInput(*this);
        if (!m_audioFileInput->start(filePath, loop)) {
            std::cerr << "Failed to start AudioFileInput: " << filePath << std::endl;
            delete m_audioFileInput; m_audioFileInput = nullptr;
            m_useAudioFileRing.store(false);
        }
    }
    else {
        m_useAudioFileRing.store(false);
        if (m_audioFileInput) {
            m_audioFileInput->stop();
            delete m_audioFileInput; m_audioFileInput = nullptr;
        }
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

// PortAudioInput callback implementation — needs full HackRfDevice definition
int PortAudioInput::audioCallback(const void *inputBuffer, void * /*outputBuffer*/, unsigned long framesPerBuffer,
                                   const PaStreamCallbackTimeInfo * /*timeInfo*/, PaStreamCallbackFlags /*statusFlags*/, void *userData)
{
    PortAudioInput *paInput = static_cast<PortAudioInput*>(userData);
    if (paInput && paInput->m_device && inputBuffer) {
        const float* audio = static_cast<const float*>(inputBuffer);
        paInput->m_device->ringWrite(audio, framesPerBuffer);
    }
    return paContinue;
}
