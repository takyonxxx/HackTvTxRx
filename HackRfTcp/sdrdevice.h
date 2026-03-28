#ifndef SDRDEVICE_H
#define SDRDEVICE_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QMutex>
#include <QTimer>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include "hacktvlib.h"

class SdrDevice : public QObject
{
    Q_OBJECT

public:
    explicit SdrDevice(QObject *parent = nullptr);
    ~SdrDevice();

    // Initialize with command-line arguments
    bool initialize(const std::vector<std::string>& args);

    // Start/Stop operations
    bool start();
    bool stop();

    // TCP Server operations
    bool startTcpServer(quint16 dataPort = 5000, quint16 controlPort = 5001);
    void stopTcpServer();
    bool isTcpServerRunning() const;
    int getConnectedClientsCount() const;
    int getConnectedControlClientsCount() const;

    // RX Configuration
    void setFrequency(uint64_t frequency_hz);
    void setSampleRate(uint32_t sample_rate);
    void setLnaGain(unsigned int gain);
    void setVgaGain(unsigned int gain);
    void setRxAmpGain(unsigned int gain);

signals:
    void statusMessage(const QString& message);
    void errorOccurred(const QString& error);
    void clientConnected(const QString& address);
    void clientDisconnected(const QString& address);
    void controlClientConnected(const QString& address);
    void controlClientDisconnected(const QString& address);
    void dataTransferred(quint64 bytes);
    void parameterChanged(const QString& param, const QString& value);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);

    void onNewControlConnection();
    void onControlClientDisconnected();
    void onControlDataReceived();
    void onControlSocketError(QAbstractSocket::SocketError error);

    void flushWriteBuffer();

private:
    void removeDisconnectedClients();
    void processControlCommand(QTcpSocket* client, const QString& command);
    QString getCurrentStatus();

    std::unique_ptr<HackTvLib> m_hackTvLib;

    // Data streaming
    QTcpServer* m_tcpServer;
    QList<QTcpSocket*> m_clients;

    // Control connection
    QTcpServer* m_controlServer;
    QList<QTcpSocket*> m_controlClients;

    std::atomic<quint64> m_totalBytesSent;
    std::atomic<quint64> m_totalBytesReceived;

    // Thread-safe write buffer: HackRF callback writes here, timer flushes to TCP
    // Eliminates Qt event queue accumulation that caused stale data on mode switch
    QMutex m_writeBufMutex;
    QByteArray m_writeBuffer;
    QTimer* m_flushTimer;

    // Current RX settings
    uint64_t m_currentFrequency;
    uint32_t m_currentSampleRate;
    unsigned int m_currentVgaGain;
    unsigned int m_currentLnaGain;
    unsigned int m_currentRxAmpGain;
};

#endif // SDRDEVICE_H
