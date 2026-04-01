#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QCommandLineParser>
#include <QHostInfo>
#include <QNetworkInterface>
#include "sdrdevice.h"

QString getLocalIPAddress()
{
    QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol &&
            address != QHostAddress::LocalHost) {
            return address.toString();
        }
    }
    return "127.0.0.1";
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    a.setApplicationName("HackRF TCP Radio Server");
    a.setApplicationVersion("3.0");

    SdrDevice hackrf;

    QObject::connect(&hackrf, &SdrDevice::statusMessage, [](const QString& msg) {
        qDebug() << "Status:" << msg;
    });

    QObject::connect(&hackrf, &SdrDevice::errorOccurred, [](const QString& error) {
        qDebug() << "Error:" << error;
    });

    QObject::connect(&hackrf, &SdrDevice::clientConnected, [](const QString& address) {
        qDebug() << "*** Data client connected:" << address;
    });

    QObject::connect(&hackrf, &SdrDevice::clientDisconnected, [](const QString& address) {
        qDebug() << "*** Data client disconnected:" << address;
    });

    QObject::connect(&hackrf, &SdrDevice::controlClientConnected, [](const QString& address) {
        qDebug() << "*** Control client connected:" << address;
    });

    QObject::connect(&hackrf, &SdrDevice::controlClientDisconnected, [](const QString& address) {
        qDebug() << "*** Control client disconnected:" << address;
    });

    QObject::connect(&hackrf, &SdrDevice::parameterChanged, [](const QString& param, const QString& value) {
        qDebug() << ">>> Parameter changed:" << param << "=" << value;
    });

    QCommandLineParser parser;
    parser.setApplicationDescription("HackRF TCP Radio Server - FM/AM Transceiver via TCP");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption dataPortOption(QStringList() << "d" << "data-port",
                                      "Data streaming port (IQ output)", "port", "5000");
    parser.addOption(dataPortOption);

    QCommandLineOption controlPortOption(QStringList() << "c" << "control-port",
                                         "Control interface port", "port", "5001");
    parser.addOption(controlPortOption);

    QCommandLineOption audioPortOption(QStringList() << "a" << "audio-port",
                                       "Audio input port (TX audio)", "port", "5002");
    parser.addOption(audioPortOption);

    QCommandLineOption sampleRateOption(QStringList() << "sample-rate" << "sr",
                                        "Initial sample rate in Hz", "rate", "2000000");
    parser.addOption(sampleRateOption);

    QCommandLineOption frequencyOption(QStringList() << "f" << "frequency",
                                       "Initial frequency in Hz", "freq", "145000000");
    parser.addOption(frequencyOption);

    QCommandLineOption modeOption(QStringList() << "m" << "mode",
                                   "Initial mode (rx or tx)", "mode", "rx");
    parser.addOption(modeOption);

    parser.process(a);

    quint16 dataPort = parser.value(dataPortOption).toUShort();
    quint16 controlPort = parser.value(controlPortOption).toUShort();
    quint16 audioPort = parser.value(audioPortOption).toUShort();
    uint32_t sampleRate = parser.value(sampleRateOption).toUInt();
    uint64_t frequency = parser.value(frequencyOption).toULongLong();
    QString mode = parser.value(modeOption).toLower();

    std::vector<std::string> hacktvArgs = {
        "-o", "hackrf",
        "--rx-tx-mode", mode.toStdString()
    };

    qDebug() << "\n========================================";
    qDebug() << "   HackRF TCP Radio Server v3.0";
    qDebug() << "========================================\n";

    qDebug() << "Configuration:";
    qDebug() << "  Data Port:      " << dataPort;
    qDebug() << "  Control Port:   " << controlPort;
    qDebug() << "  Audio Port:     " << audioPort;
    qDebug() << "  Sample Rate:    " << sampleRate << "Hz (" << sampleRate/1000000.0 << "MHz)";
    qDebug() << "  Frequency:      " << frequency << "Hz (" << frequency/1000000.0 << "MHz)";
    qDebug() << "  Mode:           " << mode;

    if (!hackrf.startTcpServer(dataPort, controlPort, audioPort)) {
        qDebug() << "\nFailed to start TCP servers";
        return 1;
    }

    if (!hackrf.initialize(hacktvArgs)) {
        qDebug() << "\nFailed to initialize HackRF";
        return 1;
    }

    hackrf.setSampleRate(sampleRate);
    hackrf.setFrequency(frequency);

    QThread::msleep(100);

    if (!hackrf.start()) {
        qDebug() << "\nFailed to start HackRF";
        return 1;
    }

    QString localIP = getLocalIPAddress();

    qDebug() << "\n========================================";
    qDebug() << "   HackRF Radio Server Started!";
    qDebug() << "========================================";
    qDebug() << "";
    qDebug() << "Server is running on IP:" << localIP;
    qDebug() << "  IQ Data Stream: " << localIP << ":" << dataPort;
    qDebug() << "  Control:        " << localIP << ":" << controlPort;
    qDebug() << "  TX Audio In:    " << localIP << ":" << audioPort;
    qDebug() << "";
    qDebug() << "Control commands: SWITCH_RX, SWITCH_TX, SET_FREQ:<Hz>, etc.";
    qDebug() << "Audio format: float32 PCM, mono, 44100 Hz";
    qDebug() << "\n========================================";
    qDebug() << "  Server Ready - Press Ctrl+C to stop";
    qDebug() << "========================================\n";

    return a.exec();
}
