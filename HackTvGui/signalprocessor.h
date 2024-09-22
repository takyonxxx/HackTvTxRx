#ifndef SIGNALPROCESSOR_H
#define SIGNALPROCESSOR_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <complex>
#include <vector>
#include <atomic>

class SignalProcessor : public QThread {
    Q_OBJECT

public:
    static const size_t COMPLEX_SAMPLE_COUNT = 131072; // 262144 / 2

    explicit SignalProcessor(QObject* parent = nullptr);
    ~SignalProcessor();

    void addSamples(const std::complex<float>* samples);
    void stop();

protected:
    void run() override;

private:
    std::vector<std::complex<float>> m_sampleBuffer;
    QMutex m_mutex;
    QWaitCondition m_condition;
    std::atomic<bool> m_running{true};
    std::atomic<bool> m_dataReady{false};

signals:
    void samplesReady(const std::vector<std::complex<float>>& samples);
};

#endif // SIGNALPROCESSOR_H
