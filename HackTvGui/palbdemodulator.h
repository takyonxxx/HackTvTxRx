
#ifndef PALBDEMODULATOR_H
#define PALBDEMODULATOR_H

#include <QObject>
#include <QImage>
#include <QDebug>
#include <complex>
#include <vector>

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

private:
    // Constants for PAL-B (adjusted for Turkey)
    static constexpr double VIDEO_CARRIER = 5.5e6;  // 5.5 MHz
    static constexpr double AUDIO_CARRIER = 5.74e6; // 5.74 MHz
    static constexpr double COLOR_SUBCARRIER = 4.43361875e6; // 4.43361875 MHz
    static constexpr int LINES_PER_FRAME = 625;
    static constexpr int VISIBLE_LINES = 576;
    static constexpr int PIXELS_PER_LINE = 720;
    static constexpr double LINE_DURATION = 64e-6;  // 64 µs
    static constexpr double FIELD_DURATION = 0.02;  // 20 ms (50 Hz)

    double sampleRate;

    std::vector<float> generateLowPassCoefficients(float sampleRate, float cutoffFreq, int numTaps);
    std::vector<float> lowPassFilter(const std::vector<float>& signal, float cutoffFreq);
    std::vector<std::complex<float>> frequencyShift(const std::vector<std::complex<float>>& signal, double shiftFreq);
    std::vector<float> amDemodulate(const std::vector<std::complex<float>>& signal);
    std::vector<float> fmDemodulate(const std::vector<std::complex<float>>& signal);
    std::vector<std::complex<float>> extractColorSignal(const std::vector<float>& videoSignal);
    std::vector<float> demodulateU(const std::vector<std::complex<float>>& colorSignal);
    std::vector<float> demodulateV(const std::vector<std::complex<float>>& colorSignal);
    QRgb yuv2rgb(float y, float u, float v);
    QImage convertToImage(const std::vector<float>& videoSignal);

    // New helper functions
    bool detectVerticalSync(const std::vector<float>& signal, size_t& syncStart);
    std::vector<float> removeVBI(const std::vector<float>& signal);
    std::vector<float> timingRecovery(const std::vector<float>& signal);
    std::vector<float> removeDCOffset(const std::vector<float>& signal);
    std::vector<float> applyAGC(const std::vector<float>& signal);
};

#endif // PALBDEMODULATOR_H
