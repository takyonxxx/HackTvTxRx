#ifndef RTLSDRDEVICE_H
#define RTLSDRDEVICE_H

#include <QObject>
#include <QByteArray>
#include <thread>
#include <rtl-sdr.h>
#include "constants.h"

class RTLSDRDevice : public QObject
{
    Q_OBJECT

public:
    explicit RTLSDRDevice(QObject *parent = nullptr);
    ~RTLSDRDevice();

    using DataCallback = std::function<void(const int8_t*, size_t)>;
    void setDataCallback(DataCallback callback);

    bool initialize(uint32_t sampleRate = DEFAULT_SAMPLE_RATE, uint32_t frequency = DEFAULT_FREQUENCY, int gain = 0);
    void start();
    void stop();

    void setFrequency(uint64_t newFrequency);

    void setSampleRate(uint32_t newSampleRate);

signals:
    void dataReady(const QByteArray &data);
    void error(const QString &message);
private:
    std::thread readThread;
    rtlsdr_dev_t *device;
    bool isRunning;
    DataCallback m_dataCallback;
    uint64_t m_frequency;
    uint32_t m_sampleRate;

    static void rtlsdrCallback(unsigned char *buf, uint32_t len, void *ctx);
};

#endif // RTLSDRDEVICE_H
