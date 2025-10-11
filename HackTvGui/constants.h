#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <QDebug>
#include <complex>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cmath>

#define _GHZ(x) ((uint64_t)(x) * 1000000000)
#define _MHZ(x) ((x) * 1000000)
#define _KHZ(x) ((x) * 1000)
#define _HZ(x) ((x) * 1)

#define DEFAULT_FREQUENCY              _MHZ(100)
#define DEFAULT_SAMPLE_RATE            _MHZ(20)
#define DEFAULT_AUDIO_SAMPLE_RATE      _KHZ(48)
#define DEFAULT_CUT_OFF                _KHZ(75)

#define HACKRF_RX_VGA_MAX_DB            62.0
#define HACKRF_RX_LNA_MAX_DB            40.0
#define HACKRF_RX_AMP_MAX_DB            11.0
#define HACKRF_TX_AMP_MAX_DB            47.0

#define DEFAULT_FFT_SIZE                1024
#define MIN_DB -60.0f
#define MAX_DB +20.0f

#define M_PI 3.14159265358979323846
#define F_PI ((float)(M_PI))

static int DEFAULT_CHANNEL_WIDTH = _KHZ(300);

inline QDebug operator<<(QDebug debug, const std::complex<float>& c)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << '(' << c.real() << ", " << c.imag() << ')';
    return debug;
}

const float PI = 3.14159265358979323846f;

// Cooley-Tukey FFT (in-place, radix-2)
inline void fft(std::vector<std::complex<float>>& x) {
    const size_t N = x.size();
    if (N <= 1) return;

    // Divide
    std::vector<std::complex<float>> even(N/2), odd(N/2);
    for (size_t i = 0; i < N/2; i++) {
        even[i] = x[2*i];
        odd[i] = x[2*i+1];
    }

    // Conquer
    fft(even);
    fft(odd);

    // Combine
    for (size_t k = 0; k < N/2; k++) {
        std::complex<float> t = std::polar(1.0f, -2 * PI * k / N) * odd[k];
        x[k] = even[k] + t;
        x[k+N/2] = even[k] - t;
    }
}

inline void getFft(const std::vector<std::complex<float>>& samples, std::vector<float>& fft_output, float& signal_level_dbfs, int fft_size)
{
    if (samples.size() < fft_size) {
        throw std::runtime_error("Input samples size is smaller than FFT size");
    }
    std::vector<std::complex<float>> fft_data(samples.begin(), samples.begin() + fft_size);
    // Hanning penceresi uygula
    for (int i = 0; i < fft_size; ++i) {
        float window = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fft_size - 1)));
        fft_data[i] *= window;
    }
    fft(fft_data);
    // FFT çıktısını yeniden düzenle (DC'yi ortaya al)
    std::rotate(fft_data.begin(), fft_data.begin() + fft_size / 2, fft_data.end());
    float maxPower = 1e-10f;
    for (int i = 0; i < fft_size; ++i) {
        float power = std::norm(fft_data[i]);
        maxPower = std::max(maxPower, power);
    }
    float amplificationFactor = 2.0; // Genliği koruyoruz
    float minDisplayPower = maxPower / 1e4; // Dinamik aralığı koruyoruz (-40 dB)
    float refLevel = 10.0f; // Referans seviyesini daha da yukarı çektik, bu sinyali daha aşağı kaydıracak
    fft_output.resize(fft_size);
    float totalDb = 0.0f;
    for (int i = 0; i < fft_size; ++i) {
        float power = std::norm(fft_data[i]);
        float db = 10.0f * std::log10(std::max(power, minDisplayPower) / maxPower);
        // dB değerini referans seviyesine göre ayarla ve ölçeklendir
        fft_output[i] = (db - refLevel) * amplificationFactor;
        totalDb += fft_output[i];
    }

    signal_level_dbfs = totalDb / fft_size;;
}

#endif // CONSTANTS_H
