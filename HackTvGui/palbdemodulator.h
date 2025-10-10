#ifndef PALBDEMODULATOR_H
#define PALBDEMODULATOR_H

#include <QObject>
#include <QImage>
#include <QDebug>
#include <complex>
#include <vector>
#include <array>

#include <vector>
#include <complex>
#include <QDebug>

class FrameBuffer
{
public:
    explicit FrameBuffer(size_t targetSize = 640000)
        : m_targetSize(targetSize)
    {
        m_buffer.reserve(targetSize * 2);  // Ekstra alan ayır
        qDebug() << "FrameBuffer created with target size:" << targetSize;
    }

    // Yeni buffer ekle - thread-safe ve güvenli
    void addBuffer(const std::vector<std::complex<float>>& newData)
    {
        if (newData.empty()) {
            qWarning() << "Attempted to add empty buffer!";
            return;
        }

        // Maksimum buffer boyutunu kontrol et (memory overflow önleme)
        const size_t MAX_BUFFER_SIZE = 10000000; // 10M örnek = ~80MB

        if (m_buffer.size() + newData.size() > MAX_BUFFER_SIZE) {
            qWarning() << "Buffer overflow protection triggered!";
            qWarning() << "Current size:" << m_buffer.size()
                       << "Trying to add:" << newData.size();
            // Buffer'ı sıfırla
            m_buffer.clear();
            qWarning() << "Buffer cleared to prevent overflow";
        }

        try {
            // Güvenli ekleme
            m_buffer.insert(m_buffer.end(), newData.begin(), newData.end());
        }
        catch (const std::exception& e) {
            qCritical() << "Exception in addBuffer:" << e.what();
            m_buffer.clear();
        }
    }

    // Tam frame hazır mı?
    bool isFrameReady() const
    {
        return m_buffer.size() >= m_targetSize;
    }

    // Frame'i al ve buffer'dan çıkar
    std::vector<std::complex<float>> getFrame()
    {
        if (m_buffer.size() < m_targetSize) {
            qWarning() << "Frame not ready! Size:" << m_buffer.size()
                       << "Target:" << m_targetSize;
            return std::vector<std::complex<float>>();
        }

        // GÜVENLI YÖNTEM: İlk targetSize kadar örneği kopyala
        std::vector<std::complex<float>> frame;
        frame.reserve(m_targetSize);

        // Manuel kopyalama ile güvenli transfer
        for (size_t i = 0; i < m_targetSize && i < m_buffer.size(); ++i) {
            frame.push_back(m_buffer[i]);
        }

        // Kalanları yeni buffer'a taşı (erase yerine swap kullan)
        std::vector<std::complex<float>> remainingBuffer;
        if (m_buffer.size() > m_targetSize) {
            remainingBuffer.reserve(m_buffer.size() - m_targetSize);
            for (size_t i = m_targetSize; i < m_buffer.size(); ++i) {
                remainingBuffer.push_back(m_buffer[i]);
            }
        }

        // Swap ile güvenli değiştir
        m_buffer.swap(remainingBuffer);

        qDebug() << "Frame extracted. Remaining in buffer:" << m_buffer.size();

        return frame;
    }

    // Buffer'ı tamamen temizle
    void clear()
    {
        m_buffer.clear();
        qDebug() << "FrameBuffer cleared";
    }

    // Mevcut buffer boyutu
    size_t size() const
    {
        return m_buffer.size();
    }

    // Target boyut
    size_t targetSize() const
    {
        return m_targetSize;
    }

    // Doluluk yüzdesi
    float fillPercentage() const
    {
        return (static_cast<float>(m_buffer.size()) / m_targetSize) * 100.0f;
    }

private:
    std::vector<std::complex<float>> m_buffer;
    size_t m_targetSize;
};

class PALBDemodulator : public QObject
{
    Q_OBJECT
public:
    explicit PALBDemodulator(double _sampleRate, QObject *parent = nullptr);

    struct DemodulatedFrame {
        QImage image;
        std::vector<float> audio;
    };

    DemodulatedFrame demodulate(const std::vector<std::complex<float>>& samples);

    // Allow changing sample rate if needed
    void setSampleRate(double rate) { sampleRate = rate; }
    double getSampleRate() const { return sampleRate; }

private:
    // Constants for PAL-B (adjusted for Turkey)
    static constexpr double VIDEO_CARRIER = 5.5e6;  // 5.5 MHz (adjust based on your tuning)
    static constexpr double AUDIO_CARRIER = 5.74e6; // 5.74 MHz
    static constexpr double COLOR_SUBCARRIER = 4.43361875e6; // 4.43361875 MHz

    static constexpr int LINES_PER_FRAME = 625;
    static constexpr int VISIBLE_LINES = 576;
    static constexpr int PIXELS_PER_LINE = 720;
    static constexpr double LINE_DURATION = 64e-6;  // 64 µs
    static constexpr double FIELD_DURATION = 0.02;  // 20 ms (50 Hz)

    double sampleRate;

    // Buffers for FM demodulation
    std::array<float, 6> m_fltBufferI;
    std::array<float, 6> m_fltBufferQ;

    // Helper functions
    std::vector<std::complex<float>> frequencyShift(
        const std::vector<std::complex<float>>& signal,
        double shiftFreq);

    std::vector<float> fmDemodulateYDiff(
        const std::vector<std::complex<float>>& signal);

    std::vector<float> amDemodulate(
        const std::vector<std::complex<float>>& signal);

    std::vector<float> lowPassFilter(
        const std::vector<float>& signal,
        float cutoffFreq);

    std::vector<std::complex<float>> complexLowPassFilter(
        const std::vector<std::complex<float>>& signal,
        float cutoffFreq);

    std::vector<float> decimate(
        const std::vector<float>& signal,
        int factor);

    std::vector<float> removeDCOffset(
        const std::vector<float>& signal);

    std::vector<float> applyAGC(
        const std::vector<float>& signal);

    bool detectVerticalSync(
        const std::vector<float>& signal,
        size_t& syncStart);

    std::vector<float> removeVBI(
        const std::vector<float>& signal);

    std::vector<float> timingRecovery(
        const std::vector<float>& signal);

    QImage convertToImage(
        const std::vector<float>& videoSignal,
        float brightness,
        float contrast);

    // Helper for creating FIR filter coefficients
    std::vector<float> designLowPassFIR(
        int numTaps,
        float cutoffFreq,
        float sampleRate);
};

#endif // PALBDEMODULATOR_H
