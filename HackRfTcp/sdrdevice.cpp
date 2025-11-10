#include "sdrdevice.h"
#include <QDebug>
#include <QHostAddress>

SdrDevice::SdrDevice(QObject *parent)
    : QObject(parent)
    , m_hackTvLib(nullptr)
    , m_tcpServer(nullptr)
    , m_controlServer(nullptr)
    , m_totalBytesSent(0)
    , m_totalBytesReceived(0)
    , m_currentFrequency(100000000)
    , m_currentSampleRate(16000000)
    , m_currentVgaGain(40)
    , m_currentLnaGain(40)
    , m_currentRxAmpGain(14)
    , m_currentTxAmpGain(20)
{
    m_hackTvLib = std::make_unique<HackTvLib>(this);

    // Set up log callback
    m_hackTvLib->setLogCallback([this](const std::string& msg) {
        emit statusMessage(QString::fromStdString(msg));
        qDebug() << "HackTV:" << QString::fromStdString(msg);
    });

    // Set up data callback
    m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
        if (m_hackTvLib && data && len > 0) {
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

    if (!m_hackTvLib->setArguments(args)) {
        emit errorOccurred("Failed to set arguments");
        return false;
    }

    emit statusMessage("Initialized successfully");
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

bool SdrDevice::startTcpServer(quint16 dataPort, quint16 controlPort)
{
    // Start data server
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

// Data Server Slots
void SdrDevice::onNewConnection()
{
    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket* clientSocket = m_tcpServer->nextPendingConnection();

        if (!clientSocket) {
            continue;
        }

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
                               .arg(clientAddress)
                               .arg(m_clients.size()));
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
                           .arg(clientAddress)
                           .arg(m_clients.size()));
}

void SdrDevice::onSocketError(QAbstractSocket::SocketError error)
{
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    qDebug() << "Data socket error:" << error << client->errorString();
    m_clients.removeOne(client);
    client->deleteLater();
}

// Control Server Slots
void SdrDevice::onNewControlConnection()
{
    while (m_controlServer->hasPendingConnections()) {
        QTcpSocket* clientSocket = m_controlServer->nextPendingConnection();

        if (!clientSocket) {
            continue;
        }

        connect(clientSocket, &QTcpSocket::readyRead,
                this, &SdrDevice::onControlDataReceived);
        connect(clientSocket, &QTcpSocket::disconnected,
                this, &SdrDevice::onControlClientDisconnected);
        connect(clientSocket, &QTcpSocket::errorOccurred,
                this, &SdrDevice::onSocketError);

        m_controlClients.append(clientSocket);

        QString clientAddress = QString("%1:%2")
                                    .arg(clientSocket->peerAddress().toString())
                                    .arg(clientSocket->peerPort());

        emit controlClientConnected(clientAddress);
        emit statusMessage(QString("Control client connected: %1").arg(clientAddress));

        // Send welcome message with available commands
        QString welcome =
            "HackRF TCP Control Server v1.0\n"
            "Available commands:\n"
            "  SET_FREQ:<value>         - Set frequency in Hz (e.g., SET_FREQ:100000000)\n"
            "  SET_SAMPLE_RATE:<value>  - Set sample rate in Hz (e.g., SET_SAMPLE_RATE:16000000)\n"
            "  SET_VGA_GAIN:<value>     - Set VGA gain 0-62 (e.g., SET_VGA_GAIN:40)\n"
            "  SET_LNA_GAIN:<value>     - Set LNA gain 0-40 (e.g., SET_LNA_GAIN:40)\n"
            "  SET_RX_AMP_GAIN:<value>  - Set RX amp gain 0-14 (e.g., SET_RX_AMP_GAIN:14)\n"
            "  SET_TX_AMP_GAIN:<value>  - Set TX amp gain 0-47 (e.g., SET_TX_AMP_GAIN:20)\n"
            "  GET_STATUS               - Get current settings\n"
            "  HELP                     - Show this help\n"
            "Ready.\n";

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
    else if (cmd == "GET_STATUS") {
        response = getCurrentStatus();
    }
    else if (cmd == "HELP") {
        response =
            "Available commands:\n"
            "  SET_FREQ:<value>         - Set frequency in Hz\n"
            "  SET_SAMPLE_RATE:<value>  - Set sample rate in Hz\n"
            "  SET_VGA_GAIN:<value>     - Set VGA gain 0-62\n"
            "  SET_LNA_GAIN:<value>     - Set LNA gain 0-40\n"
            "  SET_RX_AMP_GAIN:<value>  - Set RX amp gain 0-14\n"
            "  SET_TX_AMP_GAIN:<value>  - Set TX amp gain 0-47\n"
            "  GET_STATUS               - Get current settings\n"
            "  HELP                     - Show this help\n";
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
               "  Frequency:      %1 Hz (%2 MHz)\n"
               "  Sample Rate:    %3 Hz (%4 MHz)\n"
               "  VGA Gain:       %5\n"
               "  LNA Gain:       %6\n"
               "  RX Amp Gain:    %7\n"
               "  TX Amp Gain:    %8\n"
               "  Data Clients:   %9\n"
               "  Control Clients: %10\n"
               "  Data Sent:      %11 MB\n"
               ).arg(m_currentFrequency)
        .arg(m_currentFrequency / 1000000.0, 0, 'f', 3)
        .arg(m_currentSampleRate)
        .arg(m_currentSampleRate / 1000000.0, 0, 'f', 1)
        .arg(m_currentVgaGain)
        .arg(m_currentLnaGain)
        .arg(m_currentRxAmpGain)
        .arg(m_currentTxAmpGain)
        .arg(m_clients.size())
        .arg(m_controlClients.size())
        .arg(m_totalBytesSent.load() / (1024.0 * 1024.0), 0, 'f', 2);
}

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

void SdrDevice::setAmplitude(float amplitude)
{
    if (m_hackTvLib) {
        m_hackTvLib->setAmplitude(amplitude);
    }
}

void SdrDevice::setMicEnabled(bool enabled)
{
    if (m_hackTvLib) {
        m_hackTvLib->setMicEnabled(enabled);
    }
}

void SdrDevice::setLnaGain(unsigned int gain)
{
    if (m_hackTvLib) {
        m_hackTvLib->setLnaGain(gain);
        qDebug() << "LNA gain set to:" << gain;
    }
}

void SdrDevice::setVgaGain(unsigned int gain)
{
    if (m_hackTvLib) {
        m_hackTvLib->setVgaGain(gain);
        qDebug() << "VGA gain set to:" << gain;
    }
}

void SdrDevice::setTxAmpGain(unsigned int gain)
{
    if (m_hackTvLib) {
        m_hackTvLib->setTxAmpGain(gain);
        qDebug() << "TX amp gain set to:" << gain;
    }
}

void SdrDevice::setRxAmpGain(unsigned int gain)
{
    if (m_hackTvLib) {
        m_hackTvLib->setRxAmpGain(gain);
        qDebug() << "RX amp gain set to:" << gain;
    }
}
