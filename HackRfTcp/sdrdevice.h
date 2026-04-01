#ifndef SDRDEVICE_H
#define SDRDEVICE_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
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
    bool startTcpServer(quint16 dataPort = 5000, quint16 controlPort = 5001, quint16 audioPort = 5002);
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
    void setTxAmpGain(unsigned int gain);

    // TX Configuration
    void setModulationIndex(float index);
    void setAmplitude(float amp);

    // Mode switching
    bool switchToRx();
    bool switchToTx();
    bool isTxMode() const { return m_isTxMode; }

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
    // Data server (IQ out)
    void onNewConnection();
    void onClientDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);

    // Control server
    void onNewControlConnection();
    void onControlClientDisconnected();
    void onControlDataReceived();
    void onControlSocketError(QAbstractSocket::SocketError error);

    // Audio server (TX audio in)
    void onNewAudioConnection();
    void onAudioClientDisconnected();
    void onAudioDataReceived();
    void onAudioSocketError(QAbstractSocket::SocketError error);

private:
    void handleReceivedData(const int8_t *data, size_t len);
    void broadcastData(const QByteArray& data);
    void removeDisconnectedClients();
    void processControlCommand(QTcpSocket* client, const QString& command);
    QString getCurrentStatus();

    // Re-initialize HackTvLib in a given mode
    bool reinitialize(const std::string& mode);

    std::unique_ptr<HackTvLib> m_hackTvLib;

    // Data streaming (IQ output for RX)
    QTcpServer* m_tcpServer;
    QList<QTcpSocket*> m_clients;

    // Control connection
    QTcpServer* m_controlServer;
    QList<QTcpSocket*> m_controlClients;

    // Audio input (for TX - receives audio from radio client)
    QTcpServer* m_audioServer;
    QList<QTcpSocket*> m_audioClients;

    std::atomic<quint64> m_totalBytesSent;
    std::atomic<quint64> m_totalBytesReceived;

    // Current settings
    uint64_t m_currentFrequency;
    uint32_t m_currentSampleRate;
    unsigned int m_currentVgaGain;
    unsigned int m_currentLnaGain;
    unsigned int m_currentRxAmpGain;
    unsigned int m_currentTxAmpGain;
    float m_currentModulationIndex;
    float m_currentAmplitude;

    // Mode tracking
    bool m_isTxMode;

    // Audio ring buffer for TX (receives PCM float from TCP audio clients)
    static constexpr size_t TX_AUDIO_RING_SIZE = 1048576;
    std::vector<float> m_txAudioRing;
    std::atomic<size_t> m_txRingWritePos{0};
    std::atomic<size_t> m_txRingReadPos{0};

    void txRingWrite(const float* data, size_t count);
    size_t txRingRead(float* out, size_t count);
    size_t txRingAvailable() const;
    void txRingReset();

    // Port storage for re-init
    quint16 m_dataPort;
    quint16 m_controlPort;
    quint16 m_audioPort;
};

#endif // SDRDEVICE_H
