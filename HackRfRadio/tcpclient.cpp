#include "tcpclient.h"
#include <QDebug>

TcpClient::TcpClient(QObject *parent)
    : QObject(parent)
    , m_dataSocket(new QTcpSocket(this))
    , m_controlSocket(new QTcpSocket(this))
    , m_audioSocket(new QTcpSocket(this))
    , m_dataPort(5000)
    , m_controlPort(5001)
    , m_audioPort(5002)
{
    // Data socket
    connect(m_dataSocket, &QTcpSocket::connected, this, &TcpClient::onDataConnected);
    connect(m_dataSocket, &QTcpSocket::disconnected, this, &TcpClient::onDataDisconnected);
    connect(m_dataSocket, &QTcpSocket::readyRead, this, &TcpClient::onDataReadyRead);
    connect(m_dataSocket, &QTcpSocket::errorOccurred, this, &TcpClient::onDataError);

    // Control socket
    connect(m_controlSocket, &QTcpSocket::connected, this, &TcpClient::onControlConnected);
    connect(m_controlSocket, &QTcpSocket::disconnected, this, &TcpClient::onControlDisconnected);
    connect(m_controlSocket, &QTcpSocket::readyRead, this, &TcpClient::onControlReadyRead);
    connect(m_controlSocket, &QTcpSocket::errorOccurred, this, &TcpClient::onControlError);

    // Audio socket
    connect(m_audioSocket, &QTcpSocket::connected, this, &TcpClient::onAudioConnected);
    connect(m_audioSocket, &QTcpSocket::disconnected, this, &TcpClient::onAudioDisconnected);
    connect(m_audioSocket, &QTcpSocket::errorOccurred, this, &TcpClient::onAudioError);
}

TcpClient::~TcpClient()
{
    disconnectFromServer();
}

bool TcpClient::connectToServer(const QString& host, quint16 dataPort, quint16 controlPort, quint16 audioPort)
{
    m_host = host;
    m_dataPort = dataPort;
    m_controlPort = controlPort;
    m_audioPort = audioPort;

    qDebug() << "Connecting to" << host << "data:" << dataPort << "ctrl:" << controlPort << "audio:" << audioPort;

    m_controlSocket->connectToHost(host, controlPort);
    m_dataSocket->connectToHost(host, dataPort);
    m_audioSocket->connectToHost(host, audioPort);

    return true;
}

void TcpClient::disconnectFromServer()
{
    m_connected.store(false);

    if (m_dataSocket->state() != QAbstractSocket::UnconnectedState) {
        m_dataSocket->disconnectFromHost();
    }
    if (m_controlSocket->state() != QAbstractSocket::UnconnectedState) {
        m_controlSocket->disconnectFromHost();
    }
    if (m_audioSocket->state() != QAbstractSocket::UnconnectedState) {
        m_audioSocket->disconnectFromHost();
    }
}

bool TcpClient::isConnected() const
{
    return m_connected.load();
}

// ============================================================
// Control commands
// ============================================================

void TcpClient::sendCommand(const QString& command)
{
    if (m_controlSocket->state() == QAbstractSocket::ConnectedState) {
        m_controlSocket->write((command + "\n").toUtf8());
        m_controlSocket->flush();
    }
}

void TcpClient::setFrequency(uint64_t freq_hz)
{
    sendCommand(QString("SET_FREQ:%1").arg(freq_hz));
}

void TcpClient::setSampleRate(uint32_t rate)
{
    sendCommand(QString("SET_SAMPLE_RATE:%1").arg(rate));
}

void TcpClient::setVgaGain(unsigned int gain)
{
    sendCommand(QString("SET_VGA_GAIN:%1").arg(gain));
}

void TcpClient::setLnaGain(unsigned int gain)
{
    sendCommand(QString("SET_LNA_GAIN:%1").arg(gain));
}

void TcpClient::setRxAmpGain(unsigned int gain)
{
    sendCommand(QString("SET_RX_AMP_GAIN:%1").arg(gain));
}

void TcpClient::setTxAmpGain(unsigned int gain)
{
    sendCommand(QString("SET_TX_AMP_GAIN:%1").arg(gain));
}

void TcpClient::setAmpEnable(bool enable)
{
    sendCommand(QString("SET_AMP_ENABLE:%1").arg(enable ? 1 : 0));
}

void TcpClient::setModulationIndex(float index)
{
    sendCommand(QString("SET_MODULATION_INDEX:%1").arg(index));
}

void TcpClient::setAmplitude(float amp)
{
    sendCommand(QString("SET_AMPLITUDE:%1").arg(amp));
}

void TcpClient::switchToRx()
{
    sendCommand("SWITCH_RX");
}

void TcpClient::switchToTx()
{
    sendCommand("SWITCH_TX");
}

void TcpClient::requestStatus()
{
    sendCommand("GET_STATUS");
}

void TcpClient::sendAudioData(const float* data, size_t count)
{
    if (m_audioSocket->state() == QAbstractSocket::ConnectedState && data && count > 0) {
        QByteArray audioBytes(reinterpret_cast<const char*>(data), count * sizeof(float));
        m_audioSocket->write(audioBytes);
        m_audioSocket->flush(); // Force immediate send - critical for real-time audio
    }
}

// ============================================================
// Data socket slots
// ============================================================

void TcpClient::onDataConnected()
{
    qDebug() << "Data socket connected";
    m_dataSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_dataSocket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1024 * 1024);
}

void TcpClient::onDataDisconnected()
{
    qDebug() << "Data socket disconnected";
}

void TcpClient::onDataReadyRead()
{
    QByteArray data = m_dataSocket->readAll();
    if (!data.isEmpty()) {
        emit iqDataReceived(data);
    }
}

void TcpClient::onDataError(QAbstractSocket::SocketError error)
{
    qDebug() << "Data socket error:" << error << m_dataSocket->errorString();
    emit connectionError("Data: " + m_dataSocket->errorString());
}

// ============================================================
// Control socket slots
// ============================================================

void TcpClient::onControlConnected()
{
    qDebug() << "Control socket connected";
    m_connected.store(true);
    emit connected();
}

void TcpClient::onControlDisconnected()
{
    qDebug() << "Control socket disconnected";
    m_connected.store(false);
    emit disconnected();
}

void TcpClient::onControlReadyRead()
{
    while (m_controlSocket->canReadLine()) {
        QString line = QString::fromUtf8(m_controlSocket->readLine()).trimmed();
        if (!line.isEmpty()) {
            emit controlResponseReceived(line);
        }
    }
    // Also read any remaining data that doesn't end with newline
    QByteArray remaining = m_controlSocket->readAll();
    if (!remaining.isEmpty()) {
        emit controlResponseReceived(QString::fromUtf8(remaining).trimmed());
    }
}

void TcpClient::onControlError(QAbstractSocket::SocketError error)
{
    qDebug() << "Control socket error:" << error << m_controlSocket->errorString();
    emit connectionError("Control: " + m_controlSocket->errorString());
}

// ============================================================
// Audio socket slots
// ============================================================

void TcpClient::onAudioConnected()
{
    qDebug() << "Audio socket connected";
    m_audioSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    // Smaller send buffer for lower latency audio
    m_audioSocket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 32768);
}

void TcpClient::onAudioDisconnected()
{
    qDebug() << "Audio socket disconnected";
}

void TcpClient::onAudioError(QAbstractSocket::SocketError error)
{
    qDebug() << "Audio socket error:" << error << m_audioSocket->errorString();
}
