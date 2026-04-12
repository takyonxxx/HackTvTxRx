#include "sdrdevice.h"
#include <QDebug>
#include <QHostAddress>
#include <QThread>
#include <cstring>

SdrDevice::SdrDevice(QObject *parent)
    : QObject(parent)
    , m_hackTvLib(nullptr)
    , m_tcpServer(nullptr)
    , m_controlServer(nullptr)
    , m_audioServer(nullptr)
    , m_totalBytesSent(0)
    , m_totalBytesReceived(0)
    , m_currentFrequency(100000000)
    , m_currentSampleRate(2000000)
    , m_currentVgaGain(30)
    , m_currentLnaGain(40)
    , m_currentRxAmpGain(14)
    , m_currentTxAmpGain(47)
    , m_currentAmpEnable(false)
    , m_currentModulationIndex(0.40f)
    , m_currentAmplitude(0.10f)
    , m_currentModulationType(0)
    , m_isTxMode(false)
    , m_deviceType("hackrf")
    , m_dataPort(5000)
    , m_controlPort(5001)
    , m_audioPort(5002)
{
    m_txAudioRing.resize(TX_AUDIO_RING_SIZE, 0.0f);

    m_hackTvLib = std::make_unique<HackTvLib>(this);

    // Set up log callback
    m_hackTvLib->setLogCallback([this](const std::string& msg) {
        emit statusMessage(QString::fromStdString(msg));
        qDebug() << "HackTV:" << QString::fromStdString(msg);
    });

    // Set up data callback - raw IQ from HackRF -> TCP broadcast (RX mode)
    m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
        if (m_hackTvLib && data && len > 0 && !m_isTxMode) {
            QByteArray dataCopy(reinterpret_cast<const char*>(data), static_cast<int>(len));
            QMetaObject::invokeMethod(this, [this, dataCopy]() {
                handleReceivedData(reinterpret_cast<const int8_t*>(dataCopy.data()),
                                   static_cast<size_t>(dataCopy.size()));
            }, Qt::QueuedConnection);
        }
    });
}

SdrDevice::~SdrDevice()
{
    stopTcpServer();
}

bool SdrDevice::initialize(const std::vector<std::string>& args)
{
    if (!m_hackTvLib) {
        emit errorOccurred("HackTvLib not initialized");
        return false;
    }

    // Extract device type from args
    m_deviceType = "hackrf"; // default
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "-o" && i + 1 < args.size()) {
            m_deviceType = args[i + 1];
            break;
        }
    }

    if (!m_hackTvLib->setArguments(args)) {
        emit errorOccurred("Failed to set arguments");
        return false;
    }

    emit statusMessage(QString("Initialized with device: %1").arg(QString::fromStdString(m_deviceType)));
    return true;
}

bool SdrDevice::start()
{
    if (!m_hackTvLib) {
        emit errorOccurred("HackTvLib not initialized");
        return false;
    }

    if (!m_hackTvLib->start()) {
        emit errorOccurred("Failed to start");
        return false;
    }

    emit statusMessage("Started successfully");
    return true;
}

bool SdrDevice::stop()
{
    if (!m_hackTvLib) {
        return false;
    }

    if (!m_hackTvLib->stop()) {
        emit errorOccurred("Failed to stop");
        return false;
    }

    emit statusMessage("Stopped successfully");
    return true;
}

// ============================================================
// Re-initialize in a different mode
// ============================================================

bool SdrDevice::reinitialize(const std::string& mode)
{
    qDebug() << "=== reinitialize:" << QString::fromStdString(mode) << "===";

    if (m_hackTvLib) {
        m_hackTvLib->stop();
        QThread::msleep(150);
    }

    m_hackTvLib = std::make_unique<HackTvLib>(this);

    m_hackTvLib->setLogCallback([this](const std::string& msg) {
        emit statusMessage(QString::fromStdString(msg));
        qDebug() << "HackTV:" << QString::fromStdString(msg);
    });

    m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
        if (m_hackTvLib && data && len > 0 && !m_isTxMode) {
            QByteArray dataCopy(reinterpret_cast<const char*>(data), static_cast<int>(len));
            QMetaObject::invokeMethod(this, [this, dataCopy]() {
                handleReceivedData(reinterpret_cast<const int8_t*>(dataCopy.data()),
                                   static_cast<size_t>(dataCopy.size()));
            }, Qt::QueuedConnection);
        }
    });

    std::string srStr = std::to_string(m_currentSampleRate);
    std::string freqStr = std::to_string(m_currentFrequency);

    std::vector<std::string> args = {
        "-o", m_deviceType,
        "--rx-tx-mode", mode,
        "-s", srStr,
        "-f", freqStr
    };

    // Add fmtransmitter source for TX mode (only for hackrf)
    if (mode == "tx" && m_deviceType == "hackrf") {
        args.push_back("-a");
        args.push_back("fmtransmitter");
        args.push_back("--amp");  // enable RF amplifier for TX
    }

    if (!m_hackTvLib->setArguments(args)) {
        emit errorOccurred("Failed to set arguments for mode: " + QString::fromStdString(mode));
        return false;
    }

    if (!m_hackTvLib->start()) {
        emit errorOccurred("Failed to start in mode: " + QString::fromStdString(mode));
        return false;
    }

    m_hackTvLib->setSampleRate(m_currentSampleRate);
    m_hackTvLib->setFrequency(m_currentFrequency);

    if (mode == "tx") {
        QThread::msleep(200);
        m_hackTvLib->enableExternalAudioRing();

        // Pre-fill ring buffer with ~100ms of silence to prevent initial crackle
        std::vector<float> silence(4410, 0.0f);
        m_hackTvLib->writeExternalAudio(silence.data(), silence.size());

        m_hackTvLib->setModulation_index(m_currentModulationIndex);
        m_hackTvLib->setAmplitude(m_currentAmplitude);
        m_hackTvLib->setTxAmpGain(m_currentTxAmpGain);

        // Force TX AMP enable
        QThread::msleep(50);
        m_hackTvLib->setTxAmpEnable(true);

        m_hackTvLib->setTxModulationType(m_currentModulationType);

        qDebug() << "TX ready - modIdx=" << m_currentModulationIndex
                 << "amp=" << m_currentAmplitude
                 << "modType=" << m_currentModulationType
                 << "txAmpEnabled=1";
    } else {
        if (m_deviceType == "rtlsdr") {
            // RTL-SDR: use auto gain or set gain directly
            m_hackTvLib->setLnaGain(m_currentLnaGain);
        } else {
            m_hackTvLib->setVgaGain(m_currentVgaGain);
            m_hackTvLib->setLnaGain(m_currentLnaGain);
            m_hackTvLib->setRxAmpGain(m_currentRxAmpGain);
        }
    }

    return true;
}

bool SdrDevice::switchToRx()
{
    // Already in RX mode and running - skip reinitialize
    if (!m_isTxMode && m_hackTvLib && m_hackTvLib->isInitialized()) {
        qDebug() << "Already in RX mode, skipping reinitialize";
        emit statusMessage("Already in RX mode");
        return true;
    }

    qDebug() << "Switching to RX mode...";

    if (!reinitialize("rx")) {
        return false;
    }

    m_isTxMode = false;
    emit parameterChanged("Mode", "RX");
    emit statusMessage("Switched to RX mode");
    qDebug() << "RX mode active";
    return true;
}

bool SdrDevice::forceRestart()
{
    qDebug() << "Force restart with device:" << QString::fromStdString(m_deviceType);
    m_isTxMode = true;  // force switchToRx to actually reinitialize
    return switchToRx();
}

bool SdrDevice::switchToTx()
{
    qDebug() << "Switching to TX mode...";

    if (m_deviceType == "rtlsdr") {
        emit errorOccurred("RTL-SDR does not support TX mode");
        return false;
    }

    txRingReset();

    if (!reinitialize("tx")) {
        return false;
    }

    m_isTxMode = true;
    emit parameterChanged("Mode", "TX");
    emit statusMessage("Switched to TX mode");
    qDebug() << "TX mode active";
    return true;
}

// ============================================================
// TCP Servers
// ============================================================

bool SdrDevice::startTcpServer(quint16 dataPort, quint16 controlPort, quint16 audioPort)
{
    m_dataPort = dataPort;
    m_controlPort = controlPort;
    m_audioPort = audioPort;

    // Start data server (IQ output)
    if (!m_tcpServer) {
        m_tcpServer = new QTcpServer(this);
        m_tcpServer->setMaxPendingConnections(10);

        connect(m_tcpServer, &QTcpServer::newConnection,
                this, &SdrDevice::onNewConnection);

        if (!m_tcpServer->listen(QHostAddress::Any, dataPort)) {
            emit errorOccurred(QString("Failed to start data server: %1").arg(m_tcpServer->errorString()));
            delete m_tcpServer;
            m_tcpServer = nullptr;
            return false;
        }

        emit statusMessage(QString("Data server started on port %1").arg(dataPort));
        qDebug() << "Data server listening on port:" << dataPort;
    }

    // Start control server
    if (!m_controlServer) {
        m_controlServer = new QTcpServer(this);
        m_controlServer->setMaxPendingConnections(5);

        connect(m_controlServer, &QTcpServer::newConnection,
                this, &SdrDevice::onNewControlConnection);

        if (!m_controlServer->listen(QHostAddress::Any, controlPort)) {
            emit errorOccurred(QString("Failed to start control server: %1").arg(m_controlServer->errorString()));
            delete m_controlServer;
            m_controlServer = nullptr;
            return false;
        }

        emit statusMessage(QString("Control server started on port %1").arg(controlPort));
        qDebug() << "Control server listening on port:" << controlPort;
    }

    // Start audio server (TX audio input)
    if (!m_audioServer) {
        m_audioServer = new QTcpServer(this);
        m_audioServer->setMaxPendingConnections(5);

        connect(m_audioServer, &QTcpServer::newConnection,
                this, &SdrDevice::onNewAudioConnection);

        if (!m_audioServer->listen(QHostAddress::Any, audioPort)) {
            emit errorOccurred(QString("Failed to start audio server: %1").arg(m_audioServer->errorString()));
            delete m_audioServer;
            m_audioServer = nullptr;
            return false;
        }

        emit statusMessage(QString("Audio server started on port %1").arg(audioPort));
        qDebug() << "Audio server listening on port:" << audioPort;
    }

    return true;
}

void SdrDevice::stopTcpServer()
{
    // Stop data server
    if (m_tcpServer) {
        for (QTcpSocket* client : m_clients) {
            client->disconnectFromHost();
            if (client->state() != QAbstractSocket::UnconnectedState) {
                client->waitForDisconnected(1000);
            }
            client->deleteLater();
        }
        m_clients.clear();
        m_tcpServer->close();
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }

    // Stop control server
    if (m_controlServer) {
        for (QTcpSocket* client : m_controlClients) {
            client->disconnectFromHost();
            if (client->state() != QAbstractSocket::UnconnectedState) {
                client->waitForDisconnected(1000);
            }
            client->deleteLater();
        }
        m_controlClients.clear();
        m_controlServer->close();
        m_controlServer->deleteLater();
        m_controlServer = nullptr;
    }

    // Stop audio server
    if (m_audioServer) {
        for (QTcpSocket* client : m_audioClients) {
            client->disconnectFromHost();
            if (client->state() != QAbstractSocket::UnconnectedState) {
                client->waitForDisconnected(1000);
            }
            client->deleteLater();
        }
        m_audioClients.clear();
        m_audioServer->close();
        m_audioServer->deleteLater();
        m_audioServer = nullptr;
    }

    emit statusMessage("TCP Servers stopped");
}

bool SdrDevice::isTcpServerRunning() const
{
    return (m_tcpServer && m_tcpServer->isListening()) &&
           (m_controlServer && m_controlServer->isListening());
}

int SdrDevice::getConnectedClientsCount() const
{
    return m_clients.size();
}

int SdrDevice::getConnectedControlClientsCount() const
{
    return m_controlClients.size();
}

// ============================================================
// Data Server Slots (IQ output)
// ============================================================

void SdrDevice::onNewConnection()
{
    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket* clientSocket = m_tcpServer->nextPendingConnection();
        if (!clientSocket) continue;

        clientSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        clientSocket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
        clientSocket->setReadBufferSize(0);
        clientSocket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 1024 * 1024);

        connect(clientSocket, &QTcpSocket::disconnected,
                this, &SdrDevice::onClientDisconnected);
        connect(clientSocket, &QTcpSocket::errorOccurred,
                this, &SdrDevice::onSocketError);

        m_clients.append(clientSocket);

        QString clientAddress = QString("%1:%2")
                                    .arg(clientSocket->peerAddress().toString())
                                    .arg(clientSocket->peerPort());

        emit clientConnected(clientAddress);
        emit statusMessage(QString("Data client connected: %1 (Total: %2)")
                               .arg(clientAddress).arg(m_clients.size()));
    }
}

void SdrDevice::onClientDisconnected()
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    QString clientAddress = QString("%1:%2")
                                .arg(client->peerAddress().toString())
                                .arg(client->peerPort());

    m_clients.removeOne(client);
    client->deleteLater();

    emit clientDisconnected(clientAddress);
    emit statusMessage(QString("Data client disconnected: %1 (Remaining: %2)")
                           .arg(clientAddress).arg(m_clients.size()));
}

void SdrDevice::onSocketError(QAbstractSocket::SocketError error)
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;
    qDebug() << "Data socket error:" << error << client->errorString();
    m_clients.removeOne(client);
    client->deleteLater();
}

// ============================================================
// Control Server Slots
// ============================================================

void SdrDevice::onNewControlConnection()
{
    while (m_controlServer->hasPendingConnections()) {
        QTcpSocket* clientSocket = m_controlServer->nextPendingConnection();
        if (!clientSocket) continue;

        connect(clientSocket, &QTcpSocket::readyRead,
                this, &SdrDevice::onControlDataReceived);
        connect(clientSocket, &QTcpSocket::disconnected,
                this, &SdrDevice::onControlClientDisconnected);
        connect(clientSocket, &QTcpSocket::errorOccurred,
                this, &SdrDevice::onControlSocketError);

        m_controlClients.append(clientSocket);

        QString clientAddress = QString("%1:%2")
                                    .arg(clientSocket->peerAddress().toString())
                                    .arg(clientSocket->peerPort());

        emit controlClientConnected(clientAddress);
        emit statusMessage(QString("Control client connected: %1").arg(clientAddress));

        QString deviceName = QString::fromStdString(m_deviceType).toUpper();
        QString welcome =
            QString("HackRF TCP IQ Server v3.0 (Radio Mode) [%1]\n"
                    "Supports RX and TX with FM/AM modulation\n"
                    "Type HELP for available commands.\n"
                    "Ready.\n").arg(deviceName);

        clientSocket->write(welcome.toUtf8());
        clientSocket->flush();
    }
}

void SdrDevice::onControlClientDisconnected()
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    QString clientAddress = QString("%1:%2")
                                .arg(client->peerAddress().toString())
                                .arg(client->peerPort());

    m_controlClients.removeOne(client);
    client->deleteLater();

    emit controlClientDisconnected(clientAddress);
    emit statusMessage(QString("Control client disconnected: %1").arg(clientAddress));
}

void SdrDevice::onControlDataReceived()
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    while (client->canReadLine()) {
        QByteArray data = client->readLine();
        QString command = QString::fromUtf8(data).trimmed();

        if (!command.isEmpty()) {
            qDebug() << "Control command received:" << command;
            processControlCommand(client, command);
        }
    }
}

void SdrDevice::onControlSocketError(QAbstractSocket::SocketError error)
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;
    qDebug() << "Control socket error:" << error << client->errorString();
    m_controlClients.removeOne(client);
    client->deleteLater();
}

// ============================================================
// Audio Server Slots (TX audio input from radio client)
// ============================================================

void SdrDevice::onNewAudioConnection()
{
    while (m_audioServer->hasPendingConnections()) {
        QTcpSocket* clientSocket = m_audioServer->nextPendingConnection();
        if (!clientSocket) continue;

        clientSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        clientSocket->setReadBufferSize(0);

        connect(clientSocket, &QTcpSocket::readyRead,
                this, &SdrDevice::onAudioDataReceived);
        connect(clientSocket, &QTcpSocket::disconnected,
                this, &SdrDevice::onAudioClientDisconnected);
        connect(clientSocket, &QTcpSocket::errorOccurred,
                this, &SdrDevice::onAudioSocketError);

        m_audioClients.append(clientSocket);

        QString clientAddress = QString("%1:%2")
                                    .arg(clientSocket->peerAddress().toString())
                                    .arg(clientSocket->peerPort());

        qDebug() << "Audio client connected:" << clientAddress;
        emit statusMessage(QString("Audio client connected: %1").arg(clientAddress));
    }
}

void SdrDevice::onAudioClientDisconnected()
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    m_audioClients.removeOne(client);
    client->deleteLater();

    qDebug() << "Audio client disconnected";
    emit statusMessage("Audio client disconnected");
}

void SdrDevice::onAudioDataReceived()
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    QByteArray data = client->readAll();
    if (data.isEmpty()) return;

    if (!m_isTxMode || !m_hackTvLib || !m_hackTvLib->isDeviceReady()) return;

    const float* monoData = reinterpret_cast<const float*>(data.constData());
    size_t monoCount = data.size() / sizeof(float);

    // Write MONO directly to HackRfDevice ring buffer via DLL
    // writeExternalAudio handles mono->stereo conversion internally
    m_hackTvLib->writeExternalAudio(monoData, monoCount);
}

void SdrDevice::onAudioSocketError(QAbstractSocket::SocketError error)
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;
    qDebug() << "Audio socket error:" << error << client->errorString();
    m_audioClients.removeOne(client);
    client->deleteLater();
}

// ============================================================
// Control Command Processing (RX + TX)
// ============================================================

void SdrDevice::processControlCommand(QTcpSocket* client, const QString& command)
{
    QStringList parts = command.split(':');
    QString cmd = parts[0].toUpper();
    QString response;

    if (cmd == "SET_FREQ" && parts.size() == 2) {
        bool ok;
        uint64_t freq = parts[1].toULongLong(&ok);
        if (ok && freq >= 1000000 && freq <= 6000000000ULL) {
            setFrequency(freq);
            m_currentFrequency = freq;
            response = QString("OK: Frequency set to %1 Hz\n").arg(freq);
            emit parameterChanged("Frequency", QString::number(freq));
        } else {
            response = "ERROR: Invalid frequency (1 MHz - 6 GHz)\n";
        }
    }
    else if (cmd == "SET_SAMPLE_RATE" && parts.size() == 2) {
        bool ok;
        uint32_t sr = parts[1].toUInt(&ok);
        if (ok && sr >= 2000000 && sr <= 20000000) {
            setSampleRate(sr);
            m_currentSampleRate = sr;
            response = QString("OK: Sample rate set to %1 Hz\n").arg(sr);
            emit parameterChanged("SampleRate", QString::number(sr));
        } else {
            response = "ERROR: Invalid sample rate (2-20 MHz)\n";
        }
    }
    else if (cmd == "SET_VGA_GAIN" && parts.size() == 2) {
        bool ok;
        unsigned int gain = parts[1].toUInt(&ok);
        if (ok && gain <= 62) {
            setVgaGain(gain);
            m_currentVgaGain = gain;
            response = QString("OK: VGA gain set to %1\n").arg(gain);
            emit parameterChanged("VgaGain", QString::number(gain));
        } else {
            response = "ERROR: Invalid VGA gain (0-62)\n";
        }
    }
    else if (cmd == "SET_LNA_GAIN" && parts.size() == 2) {
        bool ok;
        unsigned int gain = parts[1].toUInt(&ok);
        if (ok && gain <= 40) {
            setLnaGain(gain);
            m_currentLnaGain = gain;
            response = QString("OK: LNA gain set to %1\n").arg(gain);
            emit parameterChanged("LnaGain", QString::number(gain));
        } else {
            response = "ERROR: Invalid LNA gain (0-40)\n";
        }
    }
    else if (cmd == "SET_RX_AMP_GAIN" && parts.size() == 2) {
        bool ok;
        unsigned int gain = parts[1].toUInt(&ok);
        if (ok && gain <= 14) {
            setRxAmpGain(gain);
            m_currentRxAmpGain = gain;
            response = QString("OK: RX amp gain set to %1\n").arg(gain);
            emit parameterChanged("RxAmpGain", QString::number(gain));
        } else {
            response = "ERROR: Invalid RX amp gain (0-14)\n";
        }
    }
    else if (cmd == "SET_TX_AMP_GAIN" && parts.size() == 2) {
        bool ok;
        unsigned int gain = parts[1].toUInt(&ok);
        if (ok && gain <= 47) {
            setTxAmpGain(gain);
            m_currentTxAmpGain = gain;
            response = QString("OK: TX amp gain set to %1\n").arg(gain);
            emit parameterChanged("TxAmpGain", QString::number(gain));
        } else {
            response = "ERROR: Invalid TX amp gain (0-47)\n";
        }
    }
    else if (cmd == "SET_AMP_ENABLE" && parts.size() == 2) {
        bool ok;
        int val = parts[1].toInt(&ok);
        if (ok && (val == 0 || val == 1)) {
            setAmpEnable(val != 0);
            response = QString("OK: RF amp %1\n").arg(val ? "enabled" : "disabled");
            emit parameterChanged("AmpEnable", QString::number(val));
        } else {
            response = "ERROR: Invalid amp enable value (0 or 1)\n";
        }
    }
    else if (cmd == "SET_MODULATION_INDEX" && parts.size() == 2) {
        bool ok;
        float idx = parts[1].toFloat(&ok);
        if (ok && idx >= 0.1f && idx <= 20.0f) {
            setModulationIndex(idx);
            m_currentModulationIndex = idx;
            response = QString("OK: Modulation index set to %1\n").arg(idx);
            emit parameterChanged("ModulationIndex", QString::number(idx));
        } else {
            response = "ERROR: Invalid modulation index (0.1 - 20.0)\n";
        }
    }
    else if (cmd == "SET_AMPLITUDE" && parts.size() == 2) {
        bool ok;
        float amp = parts[1].toFloat(&ok);
        if (ok && amp >= 0.0f && amp <= 2.0f) {
            setAmplitude(amp);
            m_currentAmplitude = amp;
            response = QString("OK: Amplitude set to %1\n").arg(amp);
            emit parameterChanged("Amplitude", QString::number(amp));
        } else {
            response = "ERROR: Invalid amplitude (0.0 - 2.0)\n";
        }
    }
    else if (cmd == "SET_MODULATION_TYPE" && parts.size() == 2) {
        bool ok;
        int type = parts[1].toInt(&ok);
        if (ok && type >= 0 && type <= 2) {
            setTxModulationType(type);
            m_currentModulationType = type;
            QString typeName = (type == 0) ? "NFM" : (type == 1) ? "WFM" : "AM";
            response = QString("OK: TX modulation type set to %1 (%2)\n").arg(type).arg(typeName);
            emit parameterChanged("ModulationType", typeName);
        } else {
            response = "ERROR: Invalid modulation type (0=NFM, 1=WFM, 2=AM)\n";
        }
    }
    else if (cmd == "SET_DEVICE" && parts.size() == 2) {
        QString dev = parts[1].toLower().trimmed();
        if (dev == "hackrf" || dev == "rtlsdr") {
            std::string oldDevice = m_deviceType;
            m_deviceType = dev.toStdString();
            // Re-initialize with new device
            if (m_isTxMode && dev == "rtlsdr") {
                m_isTxMode = false;
            }
            QString modeName = m_isTxMode ? "tx" : "rx";
            if (reinitialize(modeName.toStdString())) {
                response = QString("OK: Device switched to %1\n").arg(dev.toUpper());
                emit parameterChanged("Device", dev.toUpper());
            } else {
                m_deviceType = oldDevice;  // revert on failure
                response = QString("ERROR: Failed to switch to %1\n").arg(dev.toUpper());
            }
        } else {
            response = "ERROR: Invalid device (hackrf or rtlsdr)\n";
        }
    }
    else if (cmd == "GET_DEVICE") {
        response = QString("DEVICE:%1\n").arg(QString::fromStdString(m_deviceType).toUpper());
    }
    else if (cmd == "SWITCH_RX") {
        if (switchToRx()) {
            response = "OK: Switched to RX mode\n";
        } else {
            response = "ERROR: Failed to switch to RX mode\n";
        }
    }
    else if (cmd == "SWITCH_TX") {
        if (m_deviceType == "rtlsdr") {
            response = "ERROR: RTL-SDR does not support TX mode\n";
        } else if (switchToTx()) {
            response = "OK: Switched to TX mode\n";
        } else {
            response = "ERROR: Failed to switch to TX mode\n";
        }
    }
    else if (cmd == "GET_STATUS") {
        response = getCurrentStatus();
    }
    else if (cmd == "HELP") {
        response =
            "Available commands:\n"
            "  SET_FREQ:<value>              - Set frequency in Hz (1 MHz - 6 GHz)\n"
            "  SET_SAMPLE_RATE:<value>       - Set sample rate / bandwidth in Hz (2-20 MHz)\n"
            "  SET_VGA_GAIN:<value>          - Set VGA gain 0-62 (RX)\n"
            "  SET_LNA_GAIN:<value>          - Set LNA gain 0-40 (RX)\n"
            "  SET_RX_AMP_GAIN:<value>       - Set RX amp gain 0-14\n"
            "  SET_TX_AMP_GAIN:<value>       - Set TX amp gain 0-47\n"
            "  SET_MODULATION_INDEX:<value>  - Set FM modulation index 0.1-20.0\n"
            "  SET_AMPLITUDE:<value>         - Set TX amplitude 0.0-2.0\n"
            "  SET_MODULATION_TYPE:<value>   - Set TX modulation 0=NFM, 1=WFM, 2=AM\n"
            "  SET_DEVICE:<type>             - Switch SDR device (hackrf or rtlsdr)\n"
            "  GET_DEVICE                    - Get current device type\n"
            "  SWITCH_RX                     - Switch to receive mode\n"
            "  SWITCH_TX                     - Switch to transmit mode\n"
            "  GET_STATUS                    - Get current settings\n"
            "  HELP                          - Show this help\n";
    }
    else {
        response = "ERROR: Unknown command. Type HELP for available commands.\n";
    }

    client->write(response.toUtf8());
    client->flush();
}

QString SdrDevice::getCurrentStatus()
{
    return QString(
               "Current Settings:\n"
               "  Device:         %1\n"
               "  Mode:           %2\n"
               "  Frequency:      %3 Hz (%4 MHz)\n"
               "  Sample Rate:    %5 Hz (%6 MHz)\n"
               "  VGA Gain:       %7\n"
               "  LNA Gain:       %8\n"
               "  RX Amp Gain:    %9\n"
               "  TX Amp Gain:    %10\n"
               "  RF Amp:         %11\n"
               "  Mod Index:      %12\n"
               "  Amplitude:      %13\n"
               "  Mod Type:       %14\n"
               "  Data Clients:   %15\n"
               "  Control Clients: %16\n"
               "  Audio Clients:  %17\n"
               "  Data Sent:      %18 MB\n"
               ).arg(QString::fromStdString(m_deviceType).toUpper())
        .arg(m_isTxMode ? "TX" : "RX")
        .arg(m_currentFrequency)
        .arg(m_currentFrequency / 1000000.0, 0, 'f', 3)
        .arg(m_currentSampleRate)
        .arg(m_currentSampleRate / 1000000.0, 0, 'f', 1)
        .arg(m_currentVgaGain)
        .arg(m_currentLnaGain)
        .arg(m_currentRxAmpGain)
        .arg(m_currentTxAmpGain)
        .arg(m_currentAmpEnable ? "ON" : "OFF")
        .arg(m_currentModulationIndex)
        .arg(m_currentAmplitude)
        .arg(m_currentModulationType == 0 ? "NFM" : m_currentModulationType == 1 ? "WFM" : "AM")
        .arg(m_clients.size())
        .arg(m_controlClients.size())
        .arg(m_audioClients.size())
        .arg(m_totalBytesSent.load() / (1024.0 * 1024.0), 0, 'f', 2);
}

// ============================================================
// Data Handling
// ============================================================

void SdrDevice::handleReceivedData(const int8_t *data, size_t len)
{
    if (!data || len == 0) return;

    m_totalBytesReceived += len;

    if (!m_clients.isEmpty()) {
        QByteArray dataArray(reinterpret_cast<const char*>(data), static_cast<int>(len));
        broadcastData(dataArray);
    }
}

void SdrDevice::broadcastData(const QByteArray& data)
{
    if (m_clients.isEmpty()) return;

    removeDisconnectedClients();

    for (QTcpSocket* client : m_clients) {
        if (client->state() == QAbstractSocket::ConnectedState) {
            qint64 bytesWritten = client->write(data);
            if (bytesWritten > 0) {
                m_totalBytesSent += bytesWritten;
            }
            client->flush();
        }
    }

    static quint64 lastEmit = 0;
    if (m_totalBytesSent - lastEmit > 10 * 1024 * 1024) {
        emit dataTransferred(m_totalBytesSent);
        lastEmit = m_totalBytesSent;
    }
}

void SdrDevice::removeDisconnectedClients()
{
    for (int i = m_clients.size() - 1; i >= 0; --i) {
        if (m_clients[i]->state() != QAbstractSocket::ConnectedState) {
            QTcpSocket* client = m_clients.takeAt(i);
            client->deleteLater();
        }
    }
}

// ============================================================
// Parameter Setters
// ============================================================

void SdrDevice::setFrequency(uint64_t frequency_hz)
{
    if (m_hackTvLib) {
        m_hackTvLib->setFrequency(frequency_hz);
        qDebug() << "Frequency set to:" << frequency_hz << "Hz";
    }
}

void SdrDevice::setSampleRate(uint32_t sample_rate)
{
    if (m_hackTvLib) {
        m_hackTvLib->setSampleRate(sample_rate);
        qDebug() << "Sample rate set to:" << sample_rate << "Hz";
    }
}

void SdrDevice::setLnaGain(unsigned int gain)
{
    m_currentLnaGain = gain;
    if (m_hackTvLib) {
        if (m_deviceType == "rtlsdr") {
            // RTL-SDR: map LNA gain to overall gain (0-49 dB in 1dB steps)
            m_hackTvLib->setLnaGain(gain);
        } else {
            m_hackTvLib->setLnaGain(gain);
        }
        qDebug() << "LNA gain set to:" << gain;
    }
}

void SdrDevice::setVgaGain(unsigned int gain)
{
    m_currentVgaGain = gain;
    if (m_hackTvLib) {
        if (m_deviceType == "rtlsdr") {
            // RTL-SDR: no separate VGA, ignore or map to gain
        } else {
            m_hackTvLib->setVgaGain(gain);
        }
        qDebug() << "VGA gain set to:" << gain;
    }
}

void SdrDevice::setRxAmpGain(unsigned int gain)
{
    m_currentRxAmpGain = gain;
    if (m_hackTvLib) {
        m_hackTvLib->setRxAmpGain(gain);
        qDebug() << "RX amp gain set to:" << gain;
    }
}

void SdrDevice::setTxAmpGain(unsigned int gain)
{
    m_currentTxAmpGain = gain;
    if (m_hackTvLib) {
        m_hackTvLib->setTxAmpGain(gain);
        qDebug() << "TX amp gain set to:" << gain;
    }
}

void SdrDevice::setAmpEnable(bool enable)
{
    m_currentAmpEnable = enable;
    if (m_hackTvLib) {
        m_hackTvLib->setAmpEnable(enable);
        qDebug() << "RF amp enable set to:" << enable;
    }
}

void SdrDevice::setModulationIndex(float index)
{
    m_currentModulationIndex = index;
    if (m_hackTvLib) {
        m_hackTvLib->setModulation_index(index);
        qDebug() << "Modulation index set to:" << index;
    }
}

void SdrDevice::setAmplitude(float amp)
{
    m_currentAmplitude = amp;
    if (m_hackTvLib) {
        m_hackTvLib->setAmplitude(amp);
        qDebug() << "Amplitude set to:" << amp;
    }
}

void SdrDevice::setTxModulationType(int type)
{
    m_currentModulationType = type;
    if (m_hackTvLib) {
        m_hackTvLib->setTxModulationType(type);
        qDebug() << "TX modulation type set to:" << type
                 << (type == 0 ? "NFM" : type == 1 ? "WFM" : "AM");
    }
}

// ============================================================
// TX Audio Ring Buffer
// ============================================================

void SdrDevice::txRingWrite(const float* data, size_t count)
{
    size_t w = m_txRingWritePos.load(std::memory_order_relaxed);
    for (size_t i = 0; i < count; i++) {
        m_txAudioRing[w] = data[i];
        w = (w + 1) % TX_AUDIO_RING_SIZE;
    }
    m_txRingWritePos.store(w, std::memory_order_release);
}

size_t SdrDevice::txRingRead(float* out, size_t count)
{
    size_t avail = txRingAvailable();
    size_t toRead = std::min(count, avail);
    size_t r = m_txRingReadPos.load(std::memory_order_relaxed);
    for (size_t i = 0; i < toRead; i++) {
        out[i] = m_txAudioRing[r];
        r = (r + 1) % TX_AUDIO_RING_SIZE;
    }
    m_txRingReadPos.store(r, std::memory_order_release);
    return toRead;
}

size_t SdrDevice::txRingAvailable() const
{
    size_t w = m_txRingWritePos.load(std::memory_order_acquire);
    size_t r = m_txRingReadPos.load(std::memory_order_acquire);
    return (w >= r) ? (w - r) : (TX_AUDIO_RING_SIZE - r + w);
}

void SdrDevice::txRingReset()
{
    m_txRingWritePos.store(0, std::memory_order_relaxed);
    m_txRingReadPos.store(0, std::memory_order_relaxed);
}
