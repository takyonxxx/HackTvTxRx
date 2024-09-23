#ifndef HACKRFDEVICE_H
#define HACKRFDEVICE_H

#include <QObject>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <libhackrf/hackrf.h>
#include <pthread.h>
#include <unistd.h>
#include "audioinput.h"
#include <functional>

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
} rf_mode;

class HackRfDevice : public QObject
{
    Q_OBJECT

public:
    explicit HackRfDevice(QObject *parent = nullptr);
    ~HackRfDevice();

    using DataCallback = std::function<void(const int8_t*, size_t)>;
    void setDataCallback(DataCallback callback);
    int apply_fm_modulation(int8_t* buffer, uint32_t length);
    std::vector<float> readStreamToSize(size_t size);
    int start(rf_mode mode);
    int stop();
    std::vector<std::string> listDevices();

    // Setters
    void setFrequency(uint64_t frequency_hz);
    void setSampleRate(uint32_t sample_rate);
    void setLnaGain(unsigned int lna_gain);
    void setVgaGain(unsigned int vga_gain);
    void setTxVgaGain(unsigned int tx_vga_gain);
    void setAmpEnable(bool enable);
    void setBasebandFilterBandwidth(uint32_t bandwidth);
    void setAntennaEnable(bool enable);

    // Getters
    uint64_t getFrequency() const;
    uint32_t getSampleRate() const;
    unsigned int getLnaGain() const;
    unsigned int getVgaGain() const;
    unsigned int getTxVgaGain() const;
    bool getAmpEnable() const;
    uint32_t getBasebandFilterBandwidth() const;
    bool getAntennaEnable() const;

private slots:
    void emitReceivedData(const int8_t *data, size_t len);

private:
    static int _tx_callback(hackrf_transfer *transfer);
    static int _rx_callback(hackrf_transfer *transfer);

    std::unique_ptr<PortAudioInput> m_audioInput;

    std::vector<std::string> device_serials;
    std::vector<int> device_board_ids;
    rf_mode mode;
    hackrf_device *h_device;
    DataCallback m_dataCallback;

    // Parameters
    uint64_t m_frequency;
    uint32_t m_sampleRate;
    unsigned int m_lnaGain;
    unsigned int m_vgaGain;
    unsigned int m_txVgaGain;
    bool m_ampEnable;
    uint32_t m_basebandFilterBandwidth;
    bool m_antennaEnable;
    dsp::stream_tx<dsp::complex_tx> stream_tx;
};

#endif // HACKRFDEVICE_H

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
