#include "hackrfdevice.h"
#include "modulation.h"
#include <iostream>

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
    m_frequency(DEFAULT_FREQUENCY),
    m_sampleRate(DEFAULT_SAMPLE_RATE),
    m_lnaGain(HACKRF_RX_LNA_MAX_DB),
    m_vgaGain(HACKRF_RX_VGA_MAX_DB),
    m_txVgaGain(HACKRF_TX_VGA_MAX_DB),
    m_ampEnable(false),    
    m_antennaEnable(false)
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
    if (h_device) {
        stop();
    }
    hackrf_exit();
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

int HackRfDevice::start(rf_mode _mode)
{
    mode = _mode;

    if (device_serials.empty()) {
        fprintf(stderr, "No HackRF devices found\n");
        return RF_ERROR;
    }

    int r = hackrf_open_by_serial(device_serials[0].c_str(), &h_device);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_open() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        return RF_ERROR;
    }

    // Apply all settings
    setFrequency(m_frequency);
    setSampleRate(m_sampleRate);
    setLnaGain(m_lnaGain);
    setVgaGain(m_vgaGain);
    setTxVgaGain(m_txVgaGain);
    setAmpEnable(m_ampEnable);
    setBasebandFilterBandwidth(hackrf_compute_baseband_filter_bw(m_sampleRate));
    setAntennaEnable(m_antennaEnable);

    if(mode == RX)
    {
        r = hackrf_start_rx(h_device, _rx_callback, this);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_start_rx() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
            return RF_ERROR;
        }
        printf("hackrf_start_rx() ok\n");
    }
    else if(mode == TX)
    {
        m_audioInput = std::make_unique<PortAudioInput>(stream_tx);

        if (!m_audioInput->start()) {
            std::cerr << "Failed to start PortAudioInput" << std::endl;
            return RF_ERROR;
        }

        r = hackrf_start_tx(h_device, _tx_callback, this);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_start_tx() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
            return RF_ERROR;
        }
        printf("hackrf_start_tx() ok\n");
    }

    std::cout << "HackRF Started" << std::endl;
    return RF_OK;
}

int HackRfDevice::stop()
{
    if (!h_device) {
        return RF_OK;
    }

    int r;

    if(mode == RX)
    {
        r = hackrf_stop_rx(h_device);
    }
    else
    {
        m_audioInput->stop();
        r = hackrf_stop_tx(h_device);
    }

    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_stop_%s() failed: %s (%d)\n",
                (mode == RX ? "rx" : "tx"),
                hackrf_error_name(static_cast<hackrf_error>(r)), r);
        return RF_ERROR;
    }

    while(hackrf_is_streaming(h_device) == HACKRF_TRUE)
    {
        usleep(100);
    }

    r = hackrf_close(h_device);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_close() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
    }

    h_device = nullptr;
    std::cout << "HackRF Stopped" << std::endl;

    return RF_OK;
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
    return device->apply_fm_modulation((int8_t *)transfer->buffer, transfer->valid_length);
}

int HackRfDevice::_rx_callback(hackrf_transfer *transfer)
{
    HackRfDevice *device = reinterpret_cast<HackRfDevice *>(transfer->rx_ctx);
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

void HackRfDevice::emitReceivedData(const int8_t *data, size_t len)
{
    if (m_dataCallback) {
        m_dataCallback(data, len);
    }
}

void HackRfDevice::setFrequency(uint64_t frequency_hz)
{
    m_frequency = frequency_hz;
    if (h_device) {
        int r = hackrf_set_freq(h_device, m_frequency);
        if (r != HACKRF_SUCCESS) {
            fprintf(stderr, "hackrf_set_freq() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        }
    }
}

void HackRfDevice::setSampleRate(uint32_t sample_rate)
{
    m_sampleRate = sample_rate;
    if (h_device) {
        int r = hackrf_set_sample_rate(h_device, m_sampleRate);
        if (r != HACKRF_SUCCESS) {
            fprintf(stderr, "hackrf_set_sample_rate() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        }
        setBasebandFilterBandwidth(hackrf_compute_baseband_filter_bw(m_sampleRate));
    }
}

void HackRfDevice::setLnaGain(unsigned int lna_gain)
{
    m_lnaGain = lna_gain;
    if (h_device) {
        int r = hackrf_set_lna_gain(h_device, m_lnaGain);
        if (r != HACKRF_SUCCESS) {
            fprintf(stderr, "hackrf_set_lna_gain() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        }
    }
}

void HackRfDevice::setVgaGain(unsigned int vga_gain)
{
    m_vgaGain = vga_gain;
    if (h_device) {
        int r = hackrf_set_vga_gain(h_device, m_vgaGain);
        if (r != HACKRF_SUCCESS) {
            fprintf(stderr, "hackrf_set_vga_gain() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        }
    }
}

void HackRfDevice::setTxVgaGain(unsigned int tx_vga_gain)
{
    m_txVgaGain = tx_vga_gain;
    if (h_device) {
        int r = hackrf_set_txvga_gain(h_device, m_txVgaGain);
        if (r != HACKRF_SUCCESS) {
            fprintf(stderr, "hackrf_set_txvga_gain() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        }
    }
}

void HackRfDevice::setAmpEnable(bool enable)
{
    m_ampEnable = enable;
    if (h_device) {
        int r = hackrf_set_amp_enable(h_device, m_ampEnable ? 1 : 0);
        if (r != HACKRF_SUCCESS) {
            fprintf(stderr, "hackrf_set_amp_enable() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        }
    }
}

void HackRfDevice::setBasebandFilterBandwidth(uint32_t bandwidth)
{
    m_basebandFilterBandwidth = bandwidth;
    if (h_device) {
        int r = hackrf_set_baseband_filter_bandwidth(h_device, m_basebandFilterBandwidth);
        if (r != HACKRF_SUCCESS) {
            fprintf(stderr, "hackrf_set_baseband_filter_bandwidth() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        }
    }
}

void HackRfDevice::setAntennaEnable(bool enable)
{
    m_antennaEnable = enable;
    if (h_device) {
        int r = hackrf_set_antenna_enable(h_device, m_antennaEnable ? 1 : 0);
        if (r != HACKRF_SUCCESS) {
            fprintf(stderr, "hackrf_set_antenna_enable() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        }
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

unsigned int HackRfDevice::getTxVgaGain() const
{
    return m_txVgaGain;
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
