#ifndef AUDIOPROCESSORTHREAD_H
#define AUDIOPROCESSORTHREAD_H

#include <QThread>
#include <QMutex>
#include <atomic>
#include "CircularBuffer.h"
#include "AudioDemodulator.h"

class AudioProcessorThread : public QThread
{
    Q_OBJECT
public:
    explicit AudioProcessorThread(CircularBuffer* buffer,
                                  AudioDemodulator* demodulator,
                                  QObject* parent = nullptr);
    ~AudioProcessorThread();

    void stopProcessing();

protected:
    void run() override;

signals:
    void bufferStats(size_t available, uint64_t dropped);

private:
    CircularBuffer* m_buffer;
    AudioDemodulator* m_demodulator;
    std::atomic<bool> m_running;

    static constexpr size_t CHUNK_SIZE = 262144;  // Same as HackRF callback
};

#endif // AUDIOPROCESSORTHREAD_H
