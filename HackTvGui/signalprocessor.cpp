#include "signalprocessor.h"
#include <algorithm>

SignalProcessor::SignalProcessor(QObject* parent)
    : QThread(parent), m_sampleBuffer(COMPLEX_SAMPLE_COUNT) {}

SignalProcessor::~SignalProcessor() {
    stop();
    wait();
}

void SignalProcessor::addSamples(const std::complex<float>* samples) {
    QMutexLocker locker(&m_mutex);
    std::copy(samples, samples + COMPLEX_SAMPLE_COUNT, m_sampleBuffer.begin());
    m_dataReady = true;
    m_condition.wakeOne();
}

void SignalProcessor::stop() {
    m_running = false;
    m_condition.wakeOne();
}

void SignalProcessor::run() {
    while (m_running) {
        std::vector<std::complex<float>> processingBuffer(COMPLEX_SAMPLE_COUNT);
        {
            QMutexLocker locker(&m_mutex);
            while (!m_dataReady && m_running) {
                m_condition.wait(&m_mutex);
            }
            if (!m_running) break;
            processingBuffer = m_sampleBuffer;
            m_dataReady = false;
        }

        emit samplesReady(processingBuffer);
    }
}
