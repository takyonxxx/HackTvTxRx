#include "DeviceController.h"
#include "hackrfdevice.h"

#include <QDebug>
#include <QStringList>

DeviceController::DeviceController(QObject* parent)
    : QObject(parent)
{
}

DeviceController::~DeviceController()
{
    stop();
}

QStringList DeviceController::listDevices()
{
    QStringList out;
    try {
        HackRfDevice probe;
        for (const std::string& s : probe.listDevices())
            out << QString::fromStdString(s);
    } catch (...) {
    }
    return out;
}

void DeviceController::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

bool DeviceController::startRx(uint32_t sampleRate, uint64_t frequencyHz, const RxGains& gains)
{
    if (m_state != State::Idle) {
        emit errorOccurred("Device is already running");
        return false;
    }
    if (m_opening) return false;          // START pressed again while opening
    struct OpeningGuard { bool& f; OpeningGuard(bool& b) : f(b) { f = true; } ~OpeningGuard() { f = false; } } openingGuard(m_opening);

    m_sampleRate = sampleRate;
    m_frequency  = frequencyHz;
    m_rxRing.clear();
    m_rxOverflow.store(0);

    try {
        m_device = std::make_unique<HackRfDevice>();
    } catch (const std::exception& e) {
        m_device.reset();
        emit errorOccurred(QString("Cannot create HackRF device: %1").arg(e.what()));
        return false;
    }

    if (!m_device->isReady()) {
        m_device.reset();
        #ifdef __ANDROID__
        emit errorOccurred("HackRF bulunamadi. HackRF'i USB-OTG kablosuyla takin ve izin istegini onaylayin.");
#else
        emit errorOccurred("No HackRF device found. Check the USB connection and driver (WinUSB / libusbK).");
#endif
        return false;
    }

    m_accepting.store(true, std::memory_order_release);
    m_device->setDataCallback([this](const int8_t* data, size_t len) { onRxData(data, len); });

    if (m_device->start(rf_mode::RX) != RF_OK) {
        m_accepting.store(false);
        m_device.reset();
        emit errorOccurred("Failed to start HackRF in RX mode (device busy or driver problem).");
        return false;
    }

    // Same order as the original PAL decoder: parameters after start()
    m_device->setSampleRate(sampleRate);
    m_device->setFrequency(frequencyHz);
    m_device->setAmpEnable(gains.amp);
    m_device->setLnaGain(static_cast<unsigned>(gains.lna));
    m_device->setVgaGain(static_cast<unsigned>(gains.vga));
    m_device->setRxAmpGain(static_cast<unsigned>(gains.rxAmp));

    emit logMessage(QString("HackRF RX started: %1 Hz @ %2 MS/s")
                        .arg(frequencyHz).arg(sampleRate / 1e6, 0, 'f', 1));
    setState(State::Rx);
    return true;
}

bool DeviceController::startTx(uint32_t sampleRate, uint64_t frequencyHz, const TxGains& gains)
{
    if (m_state != State::Idle) {
        emit errorOccurred("Device is already running");
        return false;
    }
    if (m_opening) return false;          // START pressed again while opening
    struct OpeningGuard { bool& f; OpeningGuard(bool& b) : f(b) { f = true; } ~OpeningGuard() { f = false; } } openingGuard(m_opening);

    m_sampleRate = sampleRate;
    m_frequency  = frequencyHz;

    try {
        m_device = std::make_unique<HackRfDevice>();
    } catch (const std::exception& e) {
        m_device.reset();
        emit errorOccurred(QString("Cannot create HackRF device: %1").arg(e.what()));
        return false;
    }
    if (!m_device->isReady()) {
        m_device.reset();
        #ifdef __ANDROID__
        emit errorOccurred("HackRF bulunamadi. HackRF'i USB-OTG kablosuyla takin ve izin istegini onaylayin.");
#else
        emit errorOccurred("No HackRF device found. Check the USB connection and driver (WinUSB / libusbK).");
#endif
        return false;
    }

    m_device->setTxIqRing(&m_txRing);
    m_device->setTxDataNotify(m_txNotify);
    m_device->setSampleRate(sampleRate);
    m_device->setFrequency(frequencyHz);
    m_device->setTxAmpGain(static_cast<unsigned>(gains.txVga));
    m_device->setAmpEnable(gains.amp);

    if (m_device->start(rf_mode::TX) != RF_OK) {
        m_device.reset();
        emit errorOccurred("Failed to start HackRF in TX mode (device busy or driver problem).");
        return false;
    }

    // Re-apply after start (same pattern as RX)
    m_device->setSampleRate(sampleRate);
    m_device->setFrequency(frequencyHz);
    m_device->setTxAmpGain(static_cast<unsigned>(gains.txVga));
    m_device->setAmpEnable(gains.amp);

    emit logMessage(QString("HackRF TX started: %1 Hz @ %2 MS/s, TX VGA %3 dB")
                        .arg(frequencyHz).arg(sampleRate / 1e6, 0, 'f', 1).arg(gains.txVga));
    setState(State::Tx);
    return true;
}

uint64_t DeviceController::txUnderrunBytes() const
{
    return m_device ? m_device->txUnderrunBytes() : 0;
}

void DeviceController::setTxVgaGain(int db)
{
    if (m_device && m_state == State::Tx)
        m_device->setTxAmpGain(static_cast<unsigned>(db));
}

void DeviceController::stop()
{
    if (m_state == State::Idle && !m_device) return;

    // Stop accepting data first so the USB thread cannot touch the ring
    // while the consumer is being torn down by the caller.
    m_accepting.store(false, std::memory_order_release);

    if (m_device) {
        const int r = m_device->stop();
        if (r != RF_OK)
            emit logMessage("HackRF stop() reported an error; device will be released anyway");
        m_device.reset();
    }

    emit logMessage("HackRF stopped");
    setState(State::Idle);
}

void DeviceController::onRxData(const int8_t* data, size_t len)
{
    if (!m_accepting.load(std::memory_order_acquire) || !data || len == 0)
        return;

    const size_t written = m_rxRing.write(data, len);
    if (written < len)
        m_rxOverflow.fetch_add(len - written, std::memory_order_relaxed);

    if (m_rxNotify)
        m_rxNotify();
}

void DeviceController::setFrequency(uint64_t hz)
{
    m_frequency = hz;
    if (m_device && m_state != State::Idle)
        m_device->setFrequency(hz);
}

void DeviceController::setSampleRate(uint32_t hz)
{
    m_sampleRate = hz;
    if (m_device && m_state != State::Idle)
        m_device->setSampleRate(hz);
}

void DeviceController::setLnaGain(int db)
{
    if (m_device && m_state != State::Idle)
        m_device->setLnaGain(static_cast<unsigned>(db));
}

void DeviceController::setVgaGain(int db)
{
    if (m_device && m_state != State::Idle)
        m_device->setVgaGain(static_cast<unsigned>(db));
}

void DeviceController::setRxAmpGain(int db)
{
    if (m_device && m_state != State::Idle)
        m_device->setRxAmpGain(static_cast<unsigned>(db));
}

void DeviceController::setAmpEnable(bool on)
{
    if (m_device && m_state != State::Idle)
        m_device->setAmpEnable(on);
}
