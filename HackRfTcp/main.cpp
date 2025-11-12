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
    a.setApplicationName("HackRF TCP Server");
    a.setApplicationVersion("1.0");

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

    QObject::connect(&hackrf, &SdrDevice::dataTransferred, [](quint64 bytes) {
        qDebug() << "Total data transferred:" << bytes / (1024.0 * 1024.0) << "MB";
    });

    QCommandLineParser parser;
    parser.setApplicationDescription("HackRF TCP Server - Stream IQ samples and control via TCP");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption dataPortOption(QStringList() << "d" << "data-port",
                                      "Data streaming port", "port", "5000");
    parser.addOption(dataPortOption);

    QCommandLineOption controlPortOption(QStringList() << "c" << "control-port",
                                         "Control interface port", "port", "5001");
    parser.addOption(controlPortOption);

    QCommandLineOption vgaGainOption(QStringList() << "vga-gain",
                                     "Initial VGA gain (0-62)", "gain", "40");
    parser.addOption(vgaGainOption);

    QCommandLineOption lnaGainOption(QStringList() << "lna-gain",
                                     "Initial LNA gain (0-40)", "gain", "40");
    parser.addOption(lnaGainOption);

    QCommandLineOption rxAmpGainOption(QStringList() << "rx-amp-gain",
                                       "Initial RX amp gain (0-14)", "gain", "14");
    parser.addOption(rxAmpGainOption);

    QCommandLineOption txAmpGainOption(QStringList() << "tx-amp-gain",
                                       "Initial TX amp gain (0-47)", "gain", "47");
    parser.addOption(txAmpGainOption);

    QCommandLineOption sampleRateOption(QStringList() << "sample-rate" << "sr",
                                        "Initial sample rate in Hz", "rate", "2000000");
    parser.addOption(sampleRateOption);

    QCommandLineOption frequencyOption(QStringList() << "f" << "frequency",
                                       "Initial frequency in Hz", "freq", "100000000");
    parser.addOption(frequencyOption);

    parser.process(a);

    quint16 dataPort = parser.value(dataPortOption).toUShort();
    quint16 controlPort = parser.value(controlPortOption).toUShort();
    unsigned int vgaGain = parser.value(vgaGainOption).toUInt();
    unsigned int lnaGain = parser.value(lnaGainOption).toUInt();
    unsigned int rxAmpGain = parser.value(rxAmpGainOption).toUInt();
    unsigned int txAmpGain = parser.value(txAmpGainOption).toUInt();
    uint32_t sampleRate = parser.value(sampleRateOption).toUInt();
    uint64_t frequency = parser.value(frequencyOption).toULongLong();

    std::vector<std::string> hacktvArgs = {
        "-o", "hackrf",
        "--rx-tx-mode", "rx"
    };

    qDebug() << "\n========================================";
    qDebug() << "   HackRF TCP Server v1.0";
    qDebug() << "========================================\n";

    qDebug() << "Configuration:";
    qDebug() << "  Data Port:      " << dataPort;
    qDebug() << "  Control Port:   " << controlPort;
    qDebug() << "  Sample Rate:    " << sampleRate << "Hz (" << sampleRate/1000000.0 << "MHz)";
    qDebug() << "  Frequency:      " << frequency << "Hz (" << frequency/1000000.0 << "MHz)";
    qDebug() << "  VGA Gain:       " << vgaGain;
    qDebug() << "  LNA Gain:       " << lnaGain;
    qDebug() << "  RX Amp Gain:    " << rxAmpGain;
    qDebug() << "  TX Amp Gain:    " << txAmpGain;

    if (!hackrf.startTcpServer(dataPort, controlPort)) {
        qDebug() << "\nFailed to start TCP servers";
        return 1;
    }

    if (!hackrf.initialize(hacktvArgs)) {
        qDebug() << "\nFailed to initialize HackRF";
        return 1;
    }

    hackrf.setSampleRate(sampleRate);
    hackrf.setVgaGain(vgaGain);
    hackrf.setLnaGain(lnaGain);
    hackrf.setRxAmpGain(rxAmpGain);
    hackrf.setTxAmpGain(txAmpGain);
    hackrf.setFrequency(frequency);

    QThread::msleep(100);

    if (!hackrf.start()) {
        qDebug() << "\nFailed to start HackRF";        
        return 1;
    }

    QString localIP = getLocalIPAddress();

    qDebug() << "\n========================================";
    qDebug() << "   HackRF Started Successfully!";
    qDebug() << "========================================\n";

    qDebug() << "Server is running on IP:" << localIP;
    qDebug() << "  Data Stream:    " << localIP << ":" << dataPort;
    qDebug() << "  Control:        " << localIP << ":" << controlPort;
    qDebug() << "\n========================================";
    qDebug() << "  Server Ready - Press Ctrl+C to stop";
    qDebug() << "========================================\n";

    return a.exec();
}
