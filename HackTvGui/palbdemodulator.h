#ifndef PALBDEMODULATOR_H
#define PALBDEMODULATOR_H

#include <QObject>
#include <QImage>
#include <QDebug>
#include <complex>
#include <vector>
#include <array>
#include <deque>

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

    std::vector<float> generateLowPassCoefficients(float sampleRate, float cutoffFreq, int numTaps)
    {
        std::vector<float> coeffs(numTaps);
        float normCutoff = cutoffFreq / (sampleRate / 2); // Normalized cutoff frequency (0.0 to 1.0)

        // Generate low-pass filter coefficients using the sinc function and Hamming window
        for (int i = 0; i < numTaps; ++i) {
            int middle = numTaps / 2;
            if (i == middle) {
                coeffs[i] = normCutoff;
            } else {
                float x = static_cast<float>(i - middle) * M_PI;
                coeffs[i] = sin(normCutoff * x) / x;
            }

            // Apply Hamming window
            coeffs[i] *= 0.54f - 0.46f * cos(2.0f * M_PI * i / (numTaps - 1));
        }

        // Normalize coefficients
        float sum = 0.0f;
        for (auto c : coeffs) sum += c;
        for (auto& c : coeffs) c /= sum;

        return coeffs;
    }

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

    std::vector<float> lowPassFilter(const std::vector<float>& signal, float cutoffFreq);
    std::vector<std::complex<float>> frequencyShift(const std::vector<std::complex<float>>& signal, double shiftFreq);
    std::vector<float> amDemodulate(const std::vector<std::complex<float>>& signal);
    std::vector<float> fmDemodulate(const std::vector<std::complex<float>>& signal);
    QImage convertToImage(const std::vector<float>& videoSignal);
};

#endif // PALBDEMODULATOR_H
