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
    void setTxAmpGain(unsigned int tx_vga_gain);
    void setRxAmpGain(unsigned int rx_amp_gain);
    void setAmpEnable(bool enable);
    void setBasebandFilterBandwidth(uint32_t bandwidth);
    void setAntennaEnable(bool enable);

    // Getters
    uint64_t getFrequency() const;
    uint32_t getSampleRate() const;
    unsigned int getLnaGain() const;
    unsigned int getVgaGain() const;
    unsigned int getTxAmpGain() const;
    bool getAmpEnable() const;
    uint32_t getBasebandFilterBandwidth() const;
    bool getAntennaEnable() const;

    void setAmplitude(float newAmplitude);
    void setFilter_size(float newFilter_size);
    void setModulation_index(float newModulation_index);
    void setDecimation(int newDecimation);
    void setInterpolation(float newInterpolation);
private:
    static int _tx_callback(hackrf_transfer *transfer);
    static int _rx_callback(hackrf_transfer *transfer);

    std::unique_ptr<PortAudioInput> m_audioInput;

    std::vector<std::string> device_serials;
    std::vector<int> device_board_ids;
    rf_mode mode;
    hackrf_device *h_device;
    DataCallback m_dataCallback;

    float amplitude = 1.0;
    float filter_size = 0;
    float modulation_index = 5.0;
    float interpolation = 48;
    int decimation = 1;

    // Parameters
    uint64_t m_frequency;
    uint32_t m_sampleRate;
    unsigned int m_lnaGain;
    unsigned int m_vgaGain;
    unsigned int m_txAmpGain;
    unsigned int m_rxAmpGain;
    bool m_ampEnable;
    uint32_t m_basebandFilterBandwidth;
    bool m_antennaEnable;
    dsp::stream_tx<dsp::complex_tx> stream_tx;
};

#endif // HACKRFDEVICE_H
