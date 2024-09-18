#ifndef HACKRFDEVICE_H
#define HACKRFDEVICE_H

#include <QObject>
#include <stdio.h>
#include <string.h>
#include <stdint.h>  // For fixed-width integer types
#include <stdlib.h>  // For malloc and free
#include <libhackrf/hackrf.h>
#include <pthread.h>
#include <unistd.h>
#include "audioinput.h"

#define _GHZ(x) ((uint64_t)(x) * 1000000000)
#define _MHZ(x) ((x) * 1000000)
#define _KHZ(x) ((x) * 1000)
#define _HZ(x) ((x) * 1)

#define DEFAULT_FREQUENCY              _MHZ(100)
#define DEFAULT_SAMPLE_RATE            _MHZ(20)
#define DEFAULT_AUDIO_SAMPLE_RATE      _KHZ(48)
#define DEFAULT_CUT_OFF                _KHZ(75)
#define HACKRF_TX_VGA_MAX_DB            47.0
#define HACKRF_RX_VGA_MAX_DB            40.0
#define HACKRF_RX_LNA_MAX_DB            40.0
#define HACKRF_AMP_MAX_DB               14.0
#define DEFAULT_FFT_SIZE                1024

typedef enum RfMode {
    TX,
    RX,
}rf_mode;

class HackRfDevice: public QObject
{
    Q_OBJECT
public:
    explicit HackRfDevice(QObject *parent = nullptr);
    ~HackRfDevice();

    using DataCallback = std::function<void(const int8_t*, size_t)>;
    void setDataCallback(DataCallback callback);

    int start(
        rf_mode mode,
        uint32_t sample_rate,
        uint64_t frequency_hz,
        unsigned int lna_gain,
        unsigned int vga_gain,
        unsigned int tx_gain,
        unsigned char amp_enable);

    int stop();

    std::vector<std::string> listDevices();

private slots:
    void emitReceivedData(const int8_t *data, size_t len);

private:
    static int _tx_callback(hackrf_transfer *transfer);
    static int _rx_callback(hackrf_transfer *transfer);

    std::vector<std::string> device_serials;
    std::vector<int> device_board_ids;
    rf_mode mode;
    hackrf_device *h_device;
    DataCallback m_dataCallback;
};

#endif // HACKRFDEVICE_H
