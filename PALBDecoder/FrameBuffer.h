#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <QObject>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QRecursiveMutex>
#include <QMutexLocker>
#include <vector>
#include <complex>
#include <QDebug>

class FrameBuffer
{
public:
    explicit FrameBuffer(double sampleRate = 16e6, double frameDuration = 0.04)
        : m_targetSize(0)
        , m_sampleRate(sampleRate)
        , m_frameDuration(frameDuration)
    {
        updateTargetSize();
    }

    void setSampleRate(double sampleRate)
    {
        QMutexLocker lock(&m_mutex);
        m_sampleRate = sampleRate;
        updateTargetSize();
    }

    void setFrameDuration(double duration)
    {
        QMutexLocker lock(&m_mutex);
        m_frameDuration = duration;
        updateTargetSize();
    }

    void addBuffer(const std::vector<std::complex<float>>& newData)
    {
        if (newData.empty()) return;

        QMutexLocker lock(&m_mutex);

        const qsizetype MAX_BUFFER_SIZE = 10000000;

        if (m_buffer.capacity() < MAX_BUFFER_SIZE) {
            m_buffer.reserve(MAX_BUFFER_SIZE);
        }

        qsizetype oldSize = m_buffer.size();
        m_buffer.resize(oldSize + newData.size());
        std::copy(newData.begin(), newData.end(), m_buffer.begin() + oldSize);

        if (m_buffer.size() > MAX_BUFFER_SIZE) {
            qsizetype excess = m_buffer.size() - MAX_BUFFER_SIZE;
            m_buffer.remove(0, excess);
        }
    }

    bool isFrameReady() const
    {
        QMutexLocker lock(&m_mutex);
        return m_buffer.size() >= m_targetSize;
    }

    std::vector<std::complex<float>> getFrame()
    {
        QMutexLocker lock(&m_mutex);

        if (m_buffer.size() < m_targetSize) {
            return std::vector<std::complex<float>>();
        }

        std::vector<std::complex<float>> frame(m_targetSize);
        std::copy(m_buffer.begin(), m_buffer.begin() + m_targetSize, frame.begin());
        m_buffer.remove(0, m_targetSize);

        return frame;
    }

    std::vector<std::complex<float>> getHalfFrame()
    {
        QMutexLocker lock(&m_mutex);

        qsizetype halfSize = m_targetSize / 2;

        if (m_buffer.size() < halfSize) {
            return std::vector<std::complex<float>>();
        }

        std::vector<std::complex<float>> frame(halfSize);
        std::copy(m_buffer.begin(), m_buffer.begin() + halfSize, frame.begin());

        qsizetype removeSize = halfSize / 2;
        m_buffer.remove(0, removeSize);

        return frame;
    }

    std::vector<std::complex<float>> peekSamples(qsizetype numSamples) const
    {
        QMutexLocker lock(&m_mutex);

        if (m_buffer.size() < numSamples) {
            return std::vector<std::complex<float>>();
        }

        std::vector<std::complex<float>> samples(numSamples);
        std::copy(m_buffer.begin(), m_buffer.begin() + numSamples, samples.begin());

        return samples;
    }

    std::vector<std::complex<float>> getSamples(qsizetype numSamples)
    {
        QMutexLocker lock(&m_mutex);

        if (m_buffer.size() < numSamples) {
            return std::vector<std::complex<float>>();
        }

        std::vector<std::complex<float>> samples(numSamples);
        std::copy(m_buffer.begin(), m_buffer.begin() + numSamples, samples.begin());
        m_buffer.remove(0, numSamples);

        return samples;
    }

    void clear()
    {
        QMutexLocker lock(&m_mutex);
        m_buffer.clear();
    }

    qsizetype size() const
    {
        QMutexLocker lock(&m_mutex);
        return m_buffer.size();
    }

    qsizetype targetSize() const
    {
        QMutexLocker lock(&m_mutex);
        return m_targetSize;
    }

    qsizetype halfTargetSize() const
    {
        QMutexLocker lock(&m_mutex);
        return m_targetSize / 2;
    }

    double sampleRate() const { return m_sampleRate; }
    double frameDuration() const { return m_frameDuration; }

    float fillPercentage() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_targetSize == 0) return 0.0f;
        return (static_cast<float>(m_buffer.size()) / m_targetSize) * 100.0f;
    }

    bool isHalfFrameReady() const
    {
        QMutexLocker lock(&m_mutex);
        return m_buffer.size() >= (m_targetSize / 2);
    }

private:
    void updateTargetSize()
    {
        m_targetSize = static_cast<qsizetype>(m_sampleRate * m_frameDuration);
        const qsizetype RESERVE_SIZE = m_targetSize * 3;
        if (m_buffer.capacity() < RESERVE_SIZE) {
            m_buffer.reserve(RESERVE_SIZE);
        }
        qDebug() << "FrameBuffer target size:" << m_targetSize
                 << "(" << m_frameDuration * 1000 << "ms)"
                 << "Half size:" << (m_targetSize / 2);
    }

    QList<std::complex<float>> m_buffer;
    qsizetype m_targetSize;
    double m_sampleRate;
    double m_frameDuration;
    mutable QMutex m_mutex;
};

#endif // FRAMEBUFFER_H
