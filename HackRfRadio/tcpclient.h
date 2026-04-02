#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <atomic>

class TcpClient : public QObject
{
    Q_OBJECT

public:
    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient();

    // Connection
    bool connectToServer(const QString& host, quint16 dataPort, quint16 controlPort, quint16 audioPort);
    void disconnectFromServer();
    bool isConnected() const;

    // Control commands
    void sendCommand(const QString& command);
    void setFrequency(uint64_t freq_hz);
    void setSampleRate(uint32_t rate);
    void setVgaGain(unsigned int gain);
    void setLnaGain(unsigned int gain);
    void setRxAmpGain(unsigned int gain);
    void setTxAmpGain(unsigned int gain);
    void setAmpEnable(bool enable);
    void setModulationIndex(float index);
    void setAmplitude(float amp);
    void switchToRx();
    void switchToTx();
    void requestStatus();

    // TX audio data
    void sendAudioData(const float* data, size_t count);

signals:
    void connected();
    void disconnected();
    void iqDataReceived(const QByteArray& data);
    void controlResponseReceived(const QString& response);
    void connectionError(const QString& error);

private slots:
    void onDataConnected();
    void onDataDisconnected();
    void onDataReadyRead();
    void onDataError(QAbstractSocket::SocketError error);

    void onControlConnected();
    void onControlDisconnected();
    void onControlReadyRead();
    void onControlError(QAbstractSocket::SocketError error);

    void onAudioConnected();
    void onAudioDisconnected();
    void onAudioError(QAbstractSocket::SocketError error);

private:
    QTcpSocket* m_dataSocket;
    QTcpSocket* m_controlSocket;
    QTcpSocket* m_audioSocket;

    QString m_host;
    quint16 m_dataPort;
    quint16 m_controlPort;
    quint16 m_audioPort;

    std::atomic<bool> m_connected{false};
};

#endif // TCPCLIENT_H
