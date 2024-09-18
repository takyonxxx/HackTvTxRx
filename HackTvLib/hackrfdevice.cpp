#include "hackrfdevice.h"

std::string removeZerosFromBegging(const std::string &string) {
    uint32_t i = 0;
    while (i < string.length() && string[i] == '0') {
        i++;
    }
    return string.substr(i, string.length() - i);
}

HackRfDevice::HackRfDevice(QObject *parent):
    QObject(parent)
{
    // std::unique_ptr<PortAudioInput> m_audioInput = std::make_unique<PortAudioInput>(nullptr, &s.rf);
    // if(micEnabled)
    // {
    //     if (!m_audioInput->start()) {
    //         std::cerr << "Failed to start PortAudioInput" << std::endl;
    //         return;
    //     }
    // }

    // size_t desired_size = 262144 / 2;  // Desired size for the float buffer
    // std::vector<float> float_buffer = m_audioInput->readStreamToSize(desired_size);
    // std::cout << "Final buffer size " << float_buffer.size() << std::endl;
    // m_audioInput->stop();

    int r;
    /* Print the library version number */
    fprintf(stderr, "libhackrf version: %s (%s)\n",
            hackrf_library_release(),
            hackrf_library_version());

    /* Prepare the HackRF for output */
    r = hackrf_init();
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_init() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        return;
    }

    listDevices();
}

HackRfDevice::~HackRfDevice()
{

}

std::vector<std::string> HackRfDevice::listDevices()
{
    auto list = hackrf_device_list();
    if (!list) {
        std::cout << "can not read hackrf devices list" << std::endl;
    }
    for (int i = 0; i < list->devicecount; ++i) {
        if (!list->serial_numbers[i]) {
            std::cout << "can not read hackrf serial" << std::endl;
        }
        device_serials.push_back(removeZerosFromBegging(list->serial_numbers[i]));
        device_board_ids.push_back(list->usb_board_ids[i]);
        std::cout << "Found HackRf " << removeZerosFromBegging(list->serial_numbers[i]) << " " << list->usb_board_ids[i] << std::endl;
    }
    hackrf_device_list_free(list);
    return device_serials;
}


int HackRfDevice::start(rf_mode _mode,
                        uint32_t sample_rate,
                        uint64_t frequency_hz,
                        unsigned int lna_gain,
                        unsigned int vga_gain,
                        unsigned int tx_gain,
                        unsigned char amp_enable)
{
    int r;
    uint8_t rev;
    mode = _mode;
    auto serial = device_serials[0];

    r = hackrf_open_by_serial(serial.c_str(), &h_device);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_open() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        return(RF_ERROR);
    }

    /* Print the hardware revision */
    r = hackrf_board_rev_read(h_device, &rev);
    if(r == HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf: Hardware Revision: %s\n", hackrf_board_rev_name(static_cast<hackrf_board_rev>(rev)));
    }

    r = hackrf_set_sample_rate_manual(h_device, sample_rate, 1);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_sample_rate_set() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        return(RF_ERROR);
    }

    r = hackrf_set_baseband_filter_bandwidth(h_device, hackrf_compute_baseband_filter_bw(sample_rate));
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_baseband_filter_bandwidth_set() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        return(RF_ERROR);
    }

    r = hackrf_set_freq(h_device, frequency_hz);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_set_freq() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        return(RF_ERROR);
    }

    r = hackrf_set_amp_enable(h_device, amp_enable);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_set_amp_enable() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
        return(RF_ERROR);
    }

    if(mode == RX)
    {
        r =  hackrf_set_lna_gain(h_device, lna_gain);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_set_lna_gain() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
            return(RF_ERROR);
        }

        r =   hackrf_set_vga_gain(h_device, vga_gain);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, " hackrf_set_vga_gain() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
            return(RF_ERROR);
        }

        r = hackrf_set_antenna_enable(h_device, true);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_set_antenna_enable() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
            return(RF_ERROR);
        }

        r = hackrf_start_rx(h_device, _rx_callback, this);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_start_rx() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
            return(RF_ERROR);
        }
        printf("hackrf_start_rx() ok\n");
    }
    else if(mode == TX)
    {
        r = hackrf_set_txvga_gain(h_device, tx_gain);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_set_txvga_gain() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
            return(RF_ERROR);
        }

        /* Begin transmitting */
        r = hackrf_start_tx(h_device, _tx_callback, this);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_start_tx() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
            return(RF_ERROR);
        }
        printf("hackrf_start_tx() ok\n");
    }

    std::cout << "HackRf Started" << std::endl;
    return(RF_OK);
}

int HackRfDevice::stop()
{
    int r;  

    if(mode == RX)
    {
        r = hackrf_stop_rx(h_device);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_stop_rx() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
            return(RF_ERROR);
        }
    }
    else
    {
        r = hackrf_stop_tx(h_device);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_stop_tx() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
            return(RF_ERROR);
        }
    }

    /* Wait until streaming has stopped */
    while(hackrf_is_streaming(h_device) == HACKRF_TRUE)
    {
        usleep(100);
    }

    r = hackrf_close(h_device);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_close() failed: %s (%d)\n", hackrf_error_name(static_cast<hackrf_error>(r)), r);
    }

    hackrf_exit();

    std::cout << "HackRf Stopped" << std::endl;

    return(RF_OK);
}

int HackRfDevice::_tx_callback(hackrf_transfer *transfer)
{
    HackRfDevice *device = reinterpret_cast<HackRfDevice *>(transfer->tx_ctx);
    return 0;
}

int HackRfDevice::_rx_callback(hackrf_transfer *transfer)
{
    HackRfDevice *device = reinterpret_cast<HackRfDevice *>(transfer->rx_ctx);
    auto rf_data = reinterpret_cast<int8_t*>(transfer->buffer);
    auto len = transfer->valid_length;
    if (len % 2 != 0) {
        return -1; // Invalid data length
    }

    // Invoke the data callback if it's set
    if (device->m_dataCallback) {
        device->m_dataCallback(rf_data, len);
    }

    return 0;
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
