#include "PALDecoder.h"
#include "audiodemodulator.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cstdio>
#include <random>
#include <cmath>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const int rate = 16000000;
    const size_t frameSamples = static_cast<size_t>(rate * 0.04);   // one PAL frame
    std::mt19937 rng(1);
    std::normal_distribution<float> noise(0.0f, 0.05f);
    std::vector<std::complex<float>> frame(frameSamples);
    const double w = 2.0 * M_PI * 750000.0 / rate;
    for (size_t i = 0; i < frameSamples; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double line = std::fmod(t * 15625.0, 1.0);
        const float video = (line < 0.075) ? 0.0f : 0.4f + 0.3f * static_cast<float>(std::sin(t * 2.0 * M_PI * 1.0e6));
        const float a = 0.3f + 0.5f * video;
        frame[i] = std::complex<float>(a * std::cos(w * i) + noise(rng), a * std::sin(w * i) + noise(rng));
    }

    PALDecoder dec;
    dec.setSampleRate(rate);
    dec.setTuneFrequency(480000000ULL);
    dec.setColorMode(true);
    dec.setTemporalDenoise(true);
    dec.setAfcEnabled(true);
    dec.setSyncDemod(true);
    dec.setChromaGain(4.3f);

    AudioDemodulator ad;
    ad.setSampleRate(rate);
    ad.setAudioCarrierFreq(4750000.0);
    ad.setAudioEnabled(true);

    const int N = 25;
    auto timeDec = [&](const char* label) {
        for (int i = 0; i < 3; ++i) dec.processSamples(frame);
        QElapsedTimer t; t.start();
        for (int i = 0; i < N; ++i) dec.processSamples(frame);
        std::printf("  %-26s %6.1f ms / 40 ms frame\n", label, t.nsecsElapsed() / 1e6 / N);
    };
    std::printf("PALDecoder:\n");
    timeDec("full (colour+sync+afc)");
    dec.setColorMode(false);       timeDec("colour OFF");
    dec.setSyncDemod(false);       timeDec("+ syncDemod OFF");
    dec.setAfcEnabled(false);      timeDec("+ AFC OFF");
    dec.setTemporalDenoise(false); timeDec("+ denoise OFF");

    QElapsedTimer t; t.start();
    for (int i = 0; i < N; ++i) ad.processSamples(frame);
    std::printf("AudioDemodulator:            %6.1f ms / 40 ms frame\n", t.nsecsElapsed() / 1e6 / N);
    return 0;
}
