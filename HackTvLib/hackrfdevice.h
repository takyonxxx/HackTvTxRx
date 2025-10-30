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
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>
#include "audioinput.h"
#include "types.h"

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

    // Ana fonksiyonlar
    int start(rf_mode mode);
    int stop();
    void reset();
    bool isRunning() const { return m_isRunning.load() && !m_isStopped.load(); }
    bool isStopped() const { return m_isStopped.load(); }
    bool isInitialized() const { return h_device != nullptr; }
    std::vector<std::string> listDevices();

    // Callback ayarları
    void setDataCallback(DataCallback callback);

    // Parametre setter'ları (thread-safe)
    void setFrequency(uint64_t frequency_hz);
    void setSampleRate(uint32_t sample_rate);
    void setLnaGain(unsigned int lna_gain);
    void setVgaGain(unsigned int vga_gain);
    void setTxAmpGain(unsigned int tx_vga_gain);
    void setRxAmpGain(unsigned int rx_amp_gain);
    void setAmpEnable(bool enable);
    void setBasebandFilterBandwidth(uint32_t bandwidth);
    void setAntennaEnable(bool enable);

    // Modülasyon parametreleri
    void setAmplitude(float newAmplitude);
    void setFilter_size(float newFilter_size);
    void setModulation_index(float newModulation_index);
    void setDecimation(int newDecimation);
    void setInterpolation(float newInterpolation);
    void setMicEnabled(bool enable);

    // Getter'lar
    uint64_t getFrequency() const;
    uint32_t getSampleRate() const;
    unsigned int getLnaGain() const;
    unsigned int getVgaGain() const;
    unsigned int getTxAmpGain() const;
    bool getAmpEnable() const;
    uint32_t getBasebandFilterBandwidth() const;
    bool getAntennaEnable() const;

private:
    // Callback fonksiyonları
    static int _tx_callback(hackrf_transfer *transfer);
    static int _rx_callback(hackrf_transfer *transfer);

    // Yardımcı fonksiyonlar
    int apply_fm_modulation(int8_t* buffer, uint32_t length);
    std::vector<float> readStreamToSize(size_t size);
    bool applySettings();
    void cleanup();

    // Audio input
    std::unique_ptr<PortAudioInput> m_audioInput;

    // Thread safety için mutex
    std::shared_ptr<std::mutex> m_deviceMutex;

    // Durum flag'leri (atomic)
    std::atomic<bool> m_isStopped;
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isDestroying;

    // Device bilgileri
    std::vector<std::string> device_serials;
    std::vector<hackrf_usb_board_id> device_board_ids;

    // Mevcut mod
    rf_mode mode;

    // HackRF device handle
    hackrf_device *h_device;

    // Data callback
    DataCallback m_dataCallback;

    // Modülasyon parametreleri (atomic)
    std::atomic<float> amplitude;
    std::atomic<float> filter_size;
    std::atomic<float> modulation_index;
    std::atomic<float> interpolation;
    std::atomic<int> decimation;

    // RF parametreleri
    uint64_t m_frequency;
    uint32_t m_sampleRate;
    unsigned int m_lnaGain;
    unsigned int m_vgaGain;
    unsigned int m_txAmpGain;
    unsigned int m_rxAmpGain;
    bool m_ampEnable;
    uint32_t m_basebandFilterBandwidth;
    bool m_antennaEnable;

    // Stream
    dsp::stream_tx<dsp::complex_tx> stream_tx;
};

#endif // HACKRFDEVICE_H
