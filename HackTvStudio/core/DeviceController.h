#ifndef DEVICECONTROLLER_H
#define DEVICECONTROLLER_H

#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <cstdint>

#include "SpscRing.h"

class HackRfDevice;

/* RX front-end settings (HackRF One). */
struct RxGains {
    int  lna   = 32;    // 0..40 dB, 8 dB steps
    int  vga   = 30;    // 0..62 dB, 2 dB steps
    int  rxAmp = 14;    // RX amp gain as exposed by HackRfDevice (0..14)
    bool amp   = true;  // RF amplifier on/off
};

/* TX settings (HackRF One). */
struct TxGains {
    int  txVga = 20;    // 0..47 dB
    bool amp   = false; // RF amplifier (+14 dB) on/off
};

/*
 * Single owner of the HackRF device.
 *
 * The only object that creates / starts / stops HackRfDevice. The USB
 * transfer thread writes raw int8 IQ into rxRing(); whoever consumes it
 * (RxEngine) is woken through the data-notify hook. HackRF is half-duplex,
 * so there is exactly one state at a time: Idle, Rx or Tx.
 *
 * Lives in the GUI thread; all public methods are called from there.
 */
class DeviceController : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Rx, Tx };
    Q_ENUM(State)

    explicit DeviceController(QObject* parent = nullptr);
    ~DeviceController() override;

    State state() const { return m_state; }
    bool  isRunning() const { return m_state != State::Idle; }

    bool startRx(uint32_t sampleRate, uint64_t frequencyHz, const RxGains& gains);
    bool startTx(uint32_t sampleRate, uint64_t frequencyHz, const TxGains& gains);
    void stop();

    /* Live parameter updates (no-ops while Idle). */
    void setFrequency(uint64_t hz);
    void setSampleRate(uint32_t hz);
    void setLnaGain(int db);
    void setVgaGain(int db);
    void setRxAmpGain(int db);
    void setAmpEnable(bool on);
    void setTxVgaGain(int db);

    uint64_t frequency()  const { return m_frequency; }
    uint32_t sampleRate() const { return m_sampleRate; }

    /* Raw int8 IQ ring written by the USB thread. Consumer: RxEngine. */
    SpscRing<int8_t>& rxRing() { return m_rxRing; }

    /* Called from the USB thread after every write into rxRing(). */
    void setRxDataNotify(std::function<void()> fn) { m_rxNotify = std::move(fn); }

    /* Samples dropped because the consumer fell behind (since start). */
    uint64_t rxOverflowBytes() const { return m_rxOverflow.load(std::memory_order_relaxed); }

    /* int8 IQ ring the USB thread transmits from. Producer: TxEngine. */
    SpscRing<int8_t>& txRing() { return m_txRing; }
    /* Called from the USB thread after every TX transfer (ring drained a bit). */
    void setTxDataNotify(std::function<void()> fn) { m_txNotify = std::move(fn); }
    /* Zero bytes sent because the producer fell behind (since start). */
    uint64_t txUnderrunBytes() const;

    /* Serial numbers of the HackRFs currently on the bus (empty if none). */
    static QStringList listDevices();

signals:
    void stateChanged(DeviceController::State state);
    void errorOccurred(const QString& message);
    void logMessage(const QString& message);

private:
    void onRxData(const int8_t* data, size_t len);
    void setState(State s);

    std::unique_ptr<HackRfDevice> m_device;
    State m_state = State::Idle;
    bool  m_opening = false;   // start in progress (Android USB permission wait pumps events)

    uint64_t m_frequency  = 100000000ULL;
    uint32_t m_sampleRate = 16000000;

    // 8 MiB: ~250 ms at 16 MS/s (2 bytes per IQ sample)
    SpscRing<int8_t> m_rxRing{1u << 23};
    std::function<void()> m_rxNotify;
    // 4 MiB: ~1 s at 2 MS/s, ~130 ms at 16 MS/s
    SpscRing<int8_t> m_txRing{1u << 22};
    std::function<void()> m_txNotify;
    std::atomic<uint64_t> m_rxOverflow{0};
    std::atomic<bool> m_accepting{false};
};

#endif // DEVICECONTROLLER_H
