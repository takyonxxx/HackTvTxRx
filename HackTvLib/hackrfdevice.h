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

    // Core functions
    int start(rf_mode mode);
    int stop();
    void reset();
    int hardReset();
    bool isRunning() const { return m_isRunning.load() && !m_isStopped.load(); }
    bool isStopped() const { return m_isStopped.load(); }
    bool isReady() const { return !device_serials.empty(); }
    bool isInitialized() const { return h_device != nullptr; }
    std::vector<std::string> listDevices();

    // Callback
    void setDataCallback(DataCallback callback);

    // RF parameter setters (thread-safe)
    void setFrequency(uint64_t frequency_hz);
    void setSampleRate(uint32_t sample_rate);
    void setLnaGain(unsigned int lna_gain);
    void setVgaGain(unsigned int vga_gain);
    void setTxAmpGain(unsigned int tx_vga_gain);
    void setRxAmpGain(unsigned int rx_amp_gain);
    void setAmpEnable(bool enable);
    void setBasebandFilterBandwidth(uint32_t bandwidth);
    void setAntennaEnable(bool enable);

    // Modulation parameters (for video TX)
    void setAmplitude(float newAmplitude);
    void setFilter_size(float newFilter_size);
    void setModulation_index(float newModulation_index);
    void setDecimation(int newDecimation);
    void setInterpolation(float newInterpolation);

    // Getters
    uint64_t getFrequency() const;
    uint32_t getSampleRate() const;
    unsigned int getLnaGain() const;
    unsigned int getVgaGain() const;
    unsigned int getTxAmpGain() const;
    bool getAmpEnable() const;
    uint32_t getBasebandFilterBandwidth() const;
    bool getAntennaEnable() const;

    // FM TX modulation (called from tx_callback when external audio ring is active)
    int apply_fm_modulation(int8_t* buffer, uint32_t length);

private:
    // Callback functions
    static int _tx_callback(hackrf_transfer *transfer);
    static int _rx_callback(hackrf_transfer *transfer);

    bool applySettings();
    void cleanup();

    // Thread safety
    std::shared_ptr<std::mutex> m_deviceMutex;

    // State flags (atomic)
    std::atomic<bool> m_isStopped;
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isDestroying;

    // Device info
    std::vector<std::string> device_serials;
    std::vector<hackrf_usb_board_id> device_board_ids;

    // Current mode
    rf_mode mode;

    // HackRF device handle
    hackrf_device *h_device;

    // Data callback
    DataCallback m_dataCallback;

    // Modulation parameters (atomic)
    std::atomic<float> amplitude;
    std::atomic<float> filter_size;
    std::atomic<float> modulation_index;
    std::atomic<float> interpolation;
    std::atomic<int> decimation;

    // RF parameters
    uint64_t m_frequency;
    uint32_t m_sampleRate;
    unsigned int m_lnaGain;
    unsigned int m_vgaGain;
    unsigned int m_txAmpGain;
    unsigned int m_rxAmpGain;
    bool m_ampEnable;
    uint32_t m_basebandFilterBandwidth;
    bool m_antennaEnable;

public:
    // Ring buffer for external audio (GUI feeds audio here for FM TX)
    // Lock-free SPSC: GUI writes, tx_callback reads
    static constexpr size_t AUDIO_RING_SIZE = 1048576;
    std::vector<float> m_audioRing;
    std::atomic<size_t> m_ringWritePos{0};
    std::atomic<size_t> m_ringReadPos{0};
    std::atomic<bool> m_useAudioFileRing{false};

    size_t ringAvailable() const {
        size_t w = m_ringWritePos.load(std::memory_order_acquire);
        size_t r = m_ringReadPos.load(std::memory_order_acquire);
        return (w >= r) ? (w - r) : (AUDIO_RING_SIZE - r + w);
    }

    size_t ringFree() const {
        return AUDIO_RING_SIZE - 1 - ringAvailable();
    }

    void ringWrite(const float* data, size_t count) {
        size_t w = m_ringWritePos.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; i++) {
            m_audioRing[w] = data[i];
            w = (w + 1) % AUDIO_RING_SIZE;
        }
        m_ringWritePos.store(w, std::memory_order_release);
    }

    size_t ringRead(float* out, size_t count) {
        size_t avail = ringAvailable();
        size_t toRead = std::min(count, avail);
        size_t r = m_ringReadPos.load(std::memory_order_relaxed);
        for (size_t i = 0; i < toRead; i++) {
            out[i] = m_audioRing[r];
            r = (r + 1) % AUDIO_RING_SIZE;
        }
        m_ringReadPos.store(r, std::memory_order_release);
        return toRead;
    }

    void ringReset() {
        m_ringWritePos.store(0, std::memory_order_relaxed);
        m_ringReadPos.store(0, std::memory_order_relaxed);
    }
};

#endif // HACKRFDEVICE_H
