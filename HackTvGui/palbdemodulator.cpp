#include "palbdemodulator.h"

PALBDemodulator::PALBDemodulator(double _sampleRate, QObject *parent)
    : QObject(parent), sampleRate(_sampleRate)
{
    if (sampleRate < 16e6) {
        qWarning() << "Sample rate is less than 16 MHz. Adjusting processing accordingly.";
        sampleRate = 16e6;
    }
}

PALBDemodulator::DemodulatedFrame PALBDemodulator::demodulate(const std::vector<std::complex<float>>& samples)
{
    DemodulatedFrame frame;

    // Step 1: Frequency shift to isolate the video carrier
    auto shiftedVideo = frequencyShift(samples, -VIDEO_CARRIER);

    // Step 2: AM demodulate the shifted video signal
    auto videoSignal = amDemodulate(shiftedVideo);

    // Step 3: Apply a low-pass filter to the demodulated video signal to remove high-frequency noise
    float videoCutoffFrequency = 4.5e6; // Typically, PAL video bandwidth is around 4.5 MHz
    auto filteredVideoSignal = lowPassFilter(videoSignal, videoCutoffFrequency);

    // Step 4: Generate the video frame from the filtered video signal
    frame.image = convertToImage(filteredVideoSignal);

    // Step 5: Frequency shift to isolate the audio carrier
    auto shiftedAudio = frequencyShift(samples, -AUDIO_CARRIER);

    // Step 6: FM demodulate the audio signal
    auto audioSignal = fmDemodulate(shiftedAudio);
    frame.audio = audioSignal;

    // Step 7: Apply a low-pass filter to the demodulated audio signal to remove high-frequency noise
    // float audioCutoffFrequency = 15e3; // Standard audio cutoff for PAL is 15 kHz
    // frame.audio = lowPassFilter(audioSignal, audioCutoffFrequency);

    return frame;
}

// Frequency Shifting: Mix the signal to bring the target carrier to baseband
std::vector<std::complex<float>> PALBDemodulator::frequencyShift(const std::vector<std::complex<float>>& signal, double shiftFreq)
{
    std::vector<std::complex<float>> shifted(signal.size());
    double phaseIncrement = 2 * M_PI * shiftFreq / sampleRate;
    std::complex<float> phase(1, 0);
    std::complex<float> phaseStep(cos(phaseIncrement), sin(phaseIncrement));

    for (size_t i = 0; i < signal.size(); ++i) {
        shifted[i] = signal[i] * phase;
        phase *= phaseStep;
    }

    return shifted;
}

// AM Demodulation: Extract the envelope of the signal
std::vector<float> PALBDemodulator::amDemodulate(const std::vector<std::complex<float>>& signal)
{
    std::vector<float> demodulated(signal.size());
    std::transform(signal.begin(), signal.end(), demodulated.begin(),
                   [](std::complex<float> sample) { return std::abs(sample); });
    return demodulated;
}

// FM Demodulation: Differentiate the phase to get instantaneous frequency
std::vector<float> PALBDemodulator::fmDemodulate(const std::vector<std::complex<float>>& signal)
{
    std::vector<float> demodulated(signal.size());
    for (size_t i = 1; i < signal.size(); ++i) {
        float phaseDiff = std::arg(signal[i] * std::conj(signal[i - 1]));
        demodulated[i] = phaseDiff;
    }
    return demodulated;
}

// Low-Pass Filter: Filter the signal to remove high-frequency noise
std::vector<float> PALBDemodulator::lowPassFilter(const std::vector<float>& signal, float cutoffFreq)
{
    // Define the number of taps (filter length) for the FIR filter
    int numTaps = 101; // Adjust based on the desired filter sharpness and latency
    std::vector<float> coefficients = generateLowPassCoefficients(sampleRate, cutoffFreq, numTaps);

    // Convolve the input signal with the filter coefficients
    int signalSize = signal.size();
    std::vector<float> filtered(signalSize, 0.0f);

    // Apply FIR filter
    for (int i = 0; i < signalSize; ++i) {
        float acc = 0.0f;
        for (int j = 0; j < numTaps; ++j) {
            if (i - j >= 0) {
                acc += signal[i - j] * coefficients[j];
            }
        }
        filtered[i] = acc;
    }

    return filtered;
}

// Convert the demodulated video signal to a grayscale image
QImage PALBDemodulator::convertToImage(const std::vector<float>& videoSignal)
{
    QImage image(PIXELS_PER_LINE, VISIBLE_LINES, QImage::Format_Grayscale8);
    int pixelsPerLine = PIXELS_PER_LINE;

    if (videoSignal.size() < LINES_PER_FRAME * PIXELS_PER_LINE) {
        qWarning() << "Video sinyali boyutu beklenenden küçük: " << videoSignal.size()
                   << " Beklenen: " << LINES_PER_FRAME * PIXELS_PER_LINE;
        return image;
    }

    // Rescale the video signal to fit into the image
    for (int line = 0; line < VISIBLE_LINES; ++line) {
        for (int pixel = 0; pixel < pixelsPerLine; ++pixel) {
            int index = line * pixelsPerLine + pixel;
            if (index < videoSignal.size()) {
                uint8_t value = static_cast<uint8_t>(std::clamp(videoSignal[index] * 255.0f, 0.0f, 255.0f));
                image.setPixel(pixel, line, qRgb(value, value, value));
            }
        }
    }

    return image;
}
