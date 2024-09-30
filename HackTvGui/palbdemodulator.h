#ifndef PALBDEMODULATOR_H
#define PALBDEMODULATOR_H

#include <QObject>
#include <QImage>
#include <complex>
#include <vector>
#include <array>

class PALBDemodulator : public QObject
{
    Q_OBJECT

public:
    explicit PALBDemodulator(double sampleRate, QObject *parent = nullptr);

    struct DemodulatedFrame {
        QImage image;
        std::vector<float> audio;
    };

    DemodulatedFrame demodulate(const std::vector<std::complex<float>>& samples);

private:
    // Constants for PAL-B
    static constexpr double VIDEO_CARRIER = 5.5e6;  // 5.5 MHz
    static constexpr double AUDIO_CARRIER = 5.74e6; // 5.74 MHz
    static constexpr double COLOR_SUBCARRIER = 4.43361875e6; // 4.43361875 MHz
    static constexpr int LINES_PER_FRAME = 625;
    static constexpr int VISIBLE_LINES = 576;
    static constexpr int PIXELS_PER_LINE = 720;
    static constexpr double LINE_DURATION = 64e-6;  // 64 µs
    static constexpr double FIELD_DURATION = 0.02;  // 20 ms (50 Hz)

    double sampleRate;
    std::vector<float> videoSignal;
    std::vector<float> audioSignal;
    std::array<std::array<float, PIXELS_PER_LINE>, LINES_PER_FRAME> frameBuffer;
    std::array<std::array<float, PIXELS_PER_LINE>, LINES_PER_FRAME> uBuffer;
    std::array<std::array<float, PIXELS_PER_LINE>, LINES_PER_FRAME> vBuffer;

    // Filters and oscillators
    std::vector<float> videoFilter;
    std::vector<float> audioFilter;
    std::vector<float> colorBandpassFilter;
    std::complex<float> colorBurstOscillator;
    float colorBurstPhase;

    void demodulateAM(const std::vector<std::complex<float>>& samples);
    void extractVideoSignal();
    void extractAudioSignal();
    void synchronizeHorizontal();
    void synchronizeVertical();
    void decodeColor();
    QImage createImage();

    // Helper functions
    void designFilters();
    float applyFilter(const std::vector<float>& filter, const std::vector<float>& signal, int index);
    std::complex<float> applyComplexFilter(const std::vector<float>& filter, const std::vector<std::complex<float>>& signal, int index);
    void yuv2rgb(float y, float u, float v, uint8_t& r, uint8_t& g, uint8_t& b);
};

#endif // PALBDEMODULATOR_H
