#include "palbdemodulator.h"
#include <cmath>
#include <algorithm>
#include <numeric>

PALBDemodulator::PALBDemodulator(double _sampleRate, QObject *parent)
    : QObject(parent), sampleRate(_sampleRate)
{
    m_fltBufferI.fill(0.0f);
    m_fltBufferQ.fill(0.0f);
}

std::vector<std::complex<float>> PALBDemodulator::decimateComplex(
    const std::vector<std::complex<float>>& signal,
    int factor)
{
    if (factor <= 1 || signal.empty()) {
        return signal;
    }

    std::vector<std::complex<float>> decimated;
    decimated.reserve(signal.size() / factor + 1);

    for (size_t i = 0; i < signal.size(); i += factor) {
        decimated.push_back(signal[i]);
    }

    return decimated;
}

std::vector<std::complex<float>> PALBDemodulator::removeVBIComplex(
    const std::vector<std::complex<float>>& signal)
{
    const double samplesPerLine = sampleRate * lineDuration;
    const size_t vbiSamples = static_cast<size_t>(vbiLines * samplesPerLine);

    if (signal.size() <= vbiSamples || vbiSamples >= signal.size()) {
        return signal;
    }

    try {
        std::vector<std::complex<float>> result(signal.begin() + vbiSamples, signal.end());
        return result;
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in removeVBIComplex:" << e.what();
        return signal;
    }
}

std::vector<std::complex<float>> PALBDemodulator::timingRecoveryComplex(
    const std::vector<std::complex<float>>& signal)
{
    if (signal.empty()) {
        return signal;
    }

    const double samplesPerLine = sampleRate * lineDuration;
    const size_t numLines = static_cast<size_t>(signal.size() / samplesPerLine);

    if (numLines == 0) {
        return signal;
    }

    std::vector<std::complex<float>> recovered;

    try {
        size_t reserveSize = numLines * pixelsPerLine;
        if (reserveSize > 10000000) {
            reserveSize = 10000000;
        }
        recovered.reserve(reserveSize);

        const size_t skipSamples = static_cast<size_t>(samplesPerLine * horizontalOffset);
        const double activeSamplesPerLine = samplesPerLine - skipSamples;

        for (size_t line = 0; line < numLines; ++line) {
            size_t lineStart = static_cast<size_t>(line * samplesPerLine) + skipSamples;

            if (lineStart >= signal.size()) {
                break;
            }

            for (int pixel = 0; pixel < pixelsPerLine; ++pixel) {
                double srcPos = lineStart + (pixel * activeSamplesPerLine / pixelsPerLine);
                size_t idx = static_cast<size_t>(srcPos);

                if (idx + 1 >= signal.size()) {
                    if (idx < signal.size()) {
                        recovered.push_back(signal[idx]);
                    }
                    break;
                }

                double frac = srcPos - idx;
                std::complex<float> interpolated =
                    signal[idx] * static_cast<float>(1.0 - frac) +
                    signal[idx + 1] * static_cast<float>(frac);
                recovered.push_back(interpolated);
            }
        }
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in timingRecoveryComplex:" << e.what();
        if (!recovered.empty()) {
            return recovered;
        }
        return signal;
    }

    return recovered;
}

std::pair<std::vector<float>, std::vector<float>> PALBDemodulator::extractChroma(
    const std::vector<std::complex<float>>& signal,
    size_t targetSize)
{
    std::vector<float> chromaU;
    std::vector<float> chromaV;

    if (signal.empty()) {
        chromaU.resize(targetSize, 0.0f);
        chromaV.resize(targetSize, 0.0f);
        return {chromaU, chromaV};
    }

    size_t actualSize = std::min(signal.size(), targetSize);
    chromaU.reserve(actualSize);
    chromaV.reserve(actualSize);

    double chromaFreq = COLOR_SUBCARRIER / 2.0;
    double phaseInc = 2.0 * M_PI * chromaFreq / (sampleRate / 2.0);

    bool vSwitch = false;
    double samplesPerLine = (sampleRate / 2.0) * lineDuration;
    int samplesPerLineInt = static_cast<int>(samplesPerLine);

    for (size_t i = 0; i < actualSize; ++i) {
        float phase = static_cast<float>(i * phaseInc);

        float u = signal[i].real() * std::cos(phase) + signal[i].imag() * std::sin(phase);
        float v = -signal[i].real() * std::sin(phase) + signal[i].imag() * std::cos(phase);

        if (samplesPerLineInt > 0 && i > 0 && i % samplesPerLineInt == 0) {
            vSwitch = !vSwitch;
        }

        if (vSwitch) {
            v = -v;
        }

        chromaU.push_back(u * 2.0f);
        chromaV.push_back(v * 2.0f);
    }

    while (chromaU.size() < targetSize) {
        chromaU.push_back(0.0f);
        chromaV.push_back(0.0f);
    }

    return {chromaU, chromaV};
}

QImage PALBDemodulator::convertYUVtoRGB(
    const std::vector<float>& luma,
    const std::vector<float>& chromaU,
    const std::vector<float>& chromaV)
{
    QImage image(pixelsPerLine, visibleLines, QImage::Format_RGB888);

    size_t expectedSize = pixelsPerLine * visibleLines;

    if (luma.size() < expectedSize) {
        image.fill(Qt::black);
        return image;
    }

    float minY = *std::min_element(luma.begin(), luma.begin() + expectedSize);
    float maxY = *std::max_element(luma.begin(), luma.begin() + expectedSize);

    if (maxY == minY) {
        maxY = minY + 1.0f;
    }

    for (int line = 0; line < visibleLines; ++line) {
        uchar* scanLine = image.scanLine(line);

        for (int pixel = 0; pixel < pixelsPerLine; ++pixel) {
            size_t index = line * pixelsPerLine + pixel;

            if (index < luma.size()) {
                float y = (luma[index] - minY) / (maxY - minY);
                float u = (index < chromaU.size()) ? chromaU[index] * 0.5f : 0.0f;
                float v = (index < chromaV.size()) ? chromaV[index] * 0.5f : 0.0f;

                float r = y + 1.140f * v;
                float g = y - 0.395f * u - 0.581f * v;
                float b = y + 2.032f * u;

                int red = std::clamp(static_cast<int>(r * 255.0f), 0, 255);
                int green = std::clamp(static_cast<int>(g * 255.0f), 0, 255);
                int blue = std::clamp(static_cast<int>(b * 255.0f), 0, 255);

                scanLine[pixel * 3 + 0] = static_cast<uchar>(red);
                scanLine[pixel * 3 + 1] = static_cast<uchar>(green);
                scanLine[pixel * 3 + 2] = static_cast<uchar>(blue);
            } else {
                scanLine[pixel * 3 + 0] = 0;
                scanLine[pixel * 3 + 1] = 0;
                scanLine[pixel * 3 + 2] = 0;
            }
        }
    }

    return image;
}

PALBDemodulator::DemodulatedFrame PALBDemodulator::demodulate(
    const std::vector<std::complex<float>>& samples)
{
    DemodulatedFrame frame;

    try {
        if (samples.empty()) {
            return frame;
        }

        auto shiftedVideo = frequencyShift(samples, videoCarrier);

        float videoBandwidth = 5.5e6;
        shiftedVideo = complexLowPassFilter(shiftedVideo, videoBandwidth);

        auto videoSignal = amDemodulate(shiftedVideo);

        videoSignal = decimate(videoSignal, decimationFactor);

        auto decimatedComplex = decimateComplex(shiftedVideo, decimationFactor);

        double workingSampleRate = sampleRate / decimationFactor;

        videoSignal = removeDCOffset(videoSignal);

        if (!videoSignal.empty()) {
            float sum = std::accumulate(videoSignal.begin(), videoSignal.end(), 0.0f);
            float mean = sum / videoSignal.size();

            if (mean < -0.1f) {
                for (auto& sample : videoSignal) {
                    sample = -sample;
                }
            }
        }

        videoSignal = applyAGC(videoSignal);

        size_t syncStart = 0;
        double savedRate = sampleRate;
        sampleRate = workingSampleRate;

        if (detectVerticalSync(videoSignal, syncStart)) {
            std::vector<float> syncedSignal(videoSignal.begin() + syncStart, videoSignal.end());
            videoSignal = syncedSignal;

            if (syncStart < decimatedComplex.size()) {
                std::vector<std::complex<float>> syncedComplex(
                    decimatedComplex.begin() + syncStart,
                    decimatedComplex.end()
                    );
                decimatedComplex = syncedComplex;
            }
        }

        videoSignal = removeVBI(videoSignal);
        decimatedComplex = removeVBIComplex(decimatedComplex);

        videoSignal = timingRecovery(videoSignal);
        //videoSignal = extractSingleField(videoSignal, true);
        decimatedComplex = timingRecoveryComplex(decimatedComplex);

        sampleRate = savedRate;

        auto chromaUV = extractChroma(decimatedComplex, videoSignal.size());

        QImage image = convertYUVtoRGB(videoSignal, chromaUV.first, chromaUV.second);
        frame.image = image;

        auto shiftedAudio = frequencyShift(samples, AUDIO_CARRIER);
        auto filteredAudio = complexLowPassFilter(shiftedAudio, 150e3f);

        float targetAudioSampleRate = 50e3f;
        int audioDecimationFactor = 2 * std::max(1, static_cast<int>(std::round(sampleRate / targetAudioSampleRate))) / 3;

        auto decimatedAudio = decimateComplex(filteredAudio, audioDecimationFactor);

        frame.audio = fmDemodulateYDiff(decimatedAudio);

        float audioGain = 1.0f;

        for(auto &s : frame.audio) {
            s = std::clamp(s * audioGain, -1.0f, 1.0f);
        }
    }
    catch (const std::exception& e) {
        qCritical() << "EXCEPTION in PAL demodulation:" << e.what();
        frame.image = QImage(pixelsPerLine, visibleLines, QImage::Format_RGB888);
        frame.image.fill(Qt::black);
    }
    catch (...) {
        qCritical() << "UNKNOWN EXCEPTION in PAL demodulation!";
        frame.image = QImage(pixelsPerLine, visibleLines, QImage::Format_RGB888);
        frame.image.fill(Qt::black);
    }

    return frame;
}

std::vector<float> PALBDemodulator::extractSingleField(
    const std::vector<float>& signal,
    bool oddField)
{
    if (signal.size() < pixelsPerLine * 2) {
        return signal;
    }

    std::vector<float> field;
    size_t totalLines = signal.size() / pixelsPerLine;

    // Her iki satırdan birini al (odd veya even)
    for (size_t line = oddField ? 0 : 1; line < totalLines; line += 2) {
        size_t lineStart = line * pixelsPerLine;
        size_t lineEnd = lineStart + pixelsPerLine;

        if (lineEnd <= signal.size()) {
            field.insert(field.end(),
                         signal.begin() + lineStart,
                         signal.begin() + lineEnd);
        }
    }

    return field;
}

std::vector<std::complex<float>> PALBDemodulator::frequencyShift(
    const std::vector<std::complex<float>>& signal,
    double shiftFreq)
{
    std::vector<std::complex<float>> shifted(signal.size());

    double phaseIncrement = -2.0 * M_PI * shiftFreq / sampleRate;
    std::complex<float> phase(1.0f, 0.0f);
    std::complex<float> phaseStep(std::cos(phaseIncrement), std::sin(phaseIncrement));

    for (size_t i = 0; i < signal.size(); ++i) {
        shifted[i] = signal[i] * phase;
        phase *= phaseStep;

        if (i % 10000 == 0) {
            float mag = std::abs(phase);
            if (mag > 0.0f) {
                phase /= mag;
            }
        }
    }

    return shifted;
}

std::vector<float> PALBDemodulator::designLowPassFIR(
    int numTaps,
    float cutoffFreq,
    float sampleRate)
{
    if (numTaps <= 0 || numTaps > 1000) {
        return std::vector<float>();
    }

    if (cutoffFreq <= 0 || cutoffFreq >= sampleRate / 2) {
        return std::vector<float>();
    }

    try {
        std::vector<float> h(numTaps, 0.0f);

        float fc = cutoffFreq / sampleRate;
        int center = numTaps / 2;

        for (int i = 0; i < numTaps; ++i) {
            int n = i - center;
            if (n == 0) {
                h[i] = 2.0f * fc;
            } else {
                h[i] = std::sin(2.0f * M_PI * fc * n) / (M_PI * n);
            }

            float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (numTaps - 1));
            h[i] *= window;
        }

        float sum = 0.0f;
        for (const auto& coef : h) {
            sum += coef;
        }

        if (sum != 0.0f && !std::isnan(sum) && !std::isinf(sum)) {
            for (auto& coef : h) {
                coef /= sum;
            }

        }

        return h;
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in designLowPassFIR:" << e.what();
        return std::vector<float>();
    }
}

std::vector<std::complex<float>> PALBDemodulator::complexLowPassFilter(
    const std::vector<std::complex<float>>& signal,
    float cutoffFreq)
{
    if (signal.empty()) {
        return signal;
    }

    const int numTaps = 51;

    try {
        auto h = designLowPassFIR(numTaps, cutoffFreq, sampleRate);

        if (h.empty() || h.size() != numTaps) {
            return signal;
        }

        int center = numTaps / 2;
        size_t validStart = center;
        size_t validEnd = signal.size() > center ? signal.size() - center : 0;

        if (validEnd <= validStart) {
            return signal;
        }

        std::vector<std::complex<float>> filtered(signal.size());

        std::vector<std::complex<float>> padded(signal.size() + numTaps, {0.0f, 0.0f});
        std::copy(signal.begin(), signal.end(), padded.begin() + center);

        for (size_t i = 0; i < signal.size(); ++i) {
            std::complex<float> sum(0.0f, 0.0f);
            for (int j = 0; j < numTaps; ++j) {
                sum += padded[i + j] * h[j];
            }
            filtered[i] = sum;
        }
        return filtered;
    }
    catch (const std::bad_alloc& e) {
        qCritical() << "OUT OF MEMORY in complexLowPassFilter:" << e.what();
        return signal;
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in complexLowPassFilter:" << e.what();
        return signal;
    }
}

std::vector<float> PALBDemodulator::lowPassFilter(
    const std::vector<float>& signal,
    float cutoffFreq)
{
    if (signal.empty()) {
        return signal;
    }

    const int numTaps = 51;

    try {
        auto h = designLowPassFIR(numTaps, cutoffFreq, sampleRate);

        if (h.empty() || h.size() != numTaps) {
            return signal;
        }

        int center = numTaps / 2;
        size_t validStart = center;
        size_t validEnd = signal.size() > center ? signal.size() - center : 0;

        if (validEnd <= validStart) {
            return signal;
        }

        std::vector<float> filtered = signal;

        for (size_t i = validStart; i < validEnd; ++i) {
            float sum = 0.0f;
            bool validSample = true;

            for (int j = 0; j < numTaps; ++j) {
                size_t idx = i - center + j;

                if (idx >= signal.size()) {
                    validSample = false;
                    break;
                }

                sum += signal[idx] * h[j];
            }

            if (validSample) {
                filtered[i] = sum;
            }
        }

        return filtered;
    }
    catch (const std::bad_alloc& e) {
        qCritical() << "OUT OF MEMORY in lowPassFilter:" << e.what();
        return signal;
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in lowPassFilter:" << e.what();
        return signal;
    }
}

std::vector<float> PALBDemodulator::decimate(
    const std::vector<float>& signal,
    int factor)
{
    if (factor <= 1) {
        return signal;
    }

    if (signal.empty()) {
        return signal;
    }

    std::vector<float> decimated;
    decimated.reserve(signal.size() / factor + 1);

    try {
        for (size_t i = 0; i < signal.size(); i += factor) {
            decimated.push_back(signal[i]);
        }
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in decimate:" << e.what();
        return signal;
    }

    return decimated;
}

std::vector<float> PALBDemodulator::fmDemodulateYDiff(
    const std::vector<std::complex<float>>& signal)
{
    std::vector<float> demodulated(signal.size());
    if(signal.empty()) return demodulated;

    float prevPhase = std::arg(signal[0]);

    for (size_t i = 1; i < signal.size(); ++i) {
        float phase = std::arg(signal[i]);
        float diff = phase - prevPhase;

        if(diff > M_PI) diff -= 2*M_PI;
        if(diff < -M_PI) diff += 2*M_PI;

        demodulated[i] = diff;
        prevPhase = phase;
    }

    float maxVal = 0.0f;
    for(auto s : demodulated) maxVal = std::max(maxVal, std::abs(s));
    if(maxVal > 1e-6f) {
        for(auto &s : demodulated) s /= maxVal;
    }

    return demodulated;
}

std::vector<float> PALBDemodulator::amDemodulate(
    const std::vector<std::complex<float>>& signal)
{
    std::vector<float> demodulated(signal.size());

    for (size_t i = 0; i < signal.size(); ++i)
    {
        demodulated[i] = std::abs(signal[i]);
    }

    return demodulated;
}

std::vector<float> PALBDemodulator::removeDCOffset(
    const std::vector<float>& signal)
{
    if (signal.empty()) {
        return signal;
    }

    float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) / signal.size();
    std::vector<float> corrected(signal.size());

    std::transform(signal.begin(), signal.end(), corrected.begin(),
                   [mean](float sample) { return sample - mean; });

    return corrected;
}

std::vector<float> PALBDemodulator::applyAGC(
    const std::vector<float>& signal)
{
    const float targetAmplitude = 0.7f;

    std::vector<float> agcSignal(signal.size());
    float currentGain = 1.0f;

    for (size_t i = 0; i < signal.size(); ++i) {
        float absValue = std::abs(signal[i]);
        float desiredGain = targetAmplitude / std::max(absValue, 1e-6f);

        if (currentGain > desiredGain) {
            currentGain += agcAttackRate * (desiredGain - currentGain);
        } else {
            currentGain += agcDecayRate * (desiredGain - currentGain);
        }

        currentGain = std::clamp(currentGain, 0.1f, 10.0f);
        agcSignal[i] = signal[i] * currentGain;
    }

    return agcSignal;
}

bool PALBDemodulator::detectVerticalSync(
    const std::vector<float>& signal,
    size_t& syncStart)
{
    if (signal.size() < 1000) {
        return false;
    }

    float minVal = *std::min_element(signal.begin(), signal.end());
    float maxVal = *std::max_element(signal.begin(), signal.end());
    float syncThreshold = minVal + (maxVal - minVal) * vSyncThreshold;

    const size_t minSyncDuration = static_cast<size_t>(sampleRate * 27e-6);
    const size_t maxSyncDuration = static_cast<size_t>(sampleRate * 160e-6);

    for (size_t i = 0; i < signal.size() - maxSyncDuration; ++i) {
        if (signal[i] < syncThreshold) {
            size_t lowSamples = 0;

            while (i + lowSamples < signal.size() &&
                   signal[i + lowSamples] < syncThreshold) {
                ++lowSamples;
            }

            if (lowSamples >= minSyncDuration) {
                syncStart = i;
                return true;
            }

            i += lowSamples;
        }
    }

    syncStart = 0;
    return false;
}

std::vector<float> PALBDemodulator::removeVBI(
    const std::vector<float>& signal)
{
    const double samplesPerLine = sampleRate * lineDuration;
    const size_t vbiSamples = static_cast<size_t>(vbiLines * samplesPerLine);

    if (signal.size() <= vbiSamples || vbiSamples >= signal.size()) {
        return signal;
    }

    try {
        std::vector<float> result(signal.begin() + vbiSamples, signal.end());
        return result;
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in removeVBI:" << e.what();
        return signal;
    }
}

std::vector<float> PALBDemodulator::timingRecovery(
    const std::vector<float>& signal)
{
    if (signal.empty()) {
        return signal;
    }

    const double samplesPerLine = sampleRate * lineDuration;
    const size_t numLines = static_cast<size_t>(signal.size() / samplesPerLine);

    if (numLines == 0) {
        return signal;
    }

    std::vector<float> recovered;

    try {
        size_t reserveSize = numLines * pixelsPerLine;
        if (reserveSize > 10000000) {
            reserveSize = 10000000;
        }
        recovered.reserve(reserveSize);

        const size_t skipSamples = static_cast<size_t>(samplesPerLine * horizontalOffset);
        const double activeSamplesPerLine = samplesPerLine - skipSamples;

        for (size_t line = 0; line < numLines; ++line) {
            size_t lineStart = static_cast<size_t>(line * samplesPerLine) + skipSamples;

            if (lineStart >= signal.size()) {
                break;
            }

            for (int pixel = 0; pixel < pixelsPerLine; ++pixel) {
                double srcPos = lineStart + (pixel * activeSamplesPerLine / pixelsPerLine);
                size_t idx = static_cast<size_t>(srcPos);

                if (idx + 1 >= signal.size()) {
                    if (idx < signal.size()) {
                        recovered.push_back(signal[idx]);
                    }
                    break;
                }

                double frac = srcPos - idx;
                float interpolated = signal[idx] * (1.0f - frac) +
                                     signal[idx + 1] * frac;
                recovered.push_back(interpolated);
            }
        }
    }
    catch (const std::exception& e) {
        qCritical() << "Exception in timingRecovery:" << e.what();
        if (!recovered.empty()) {
            return recovered;
        }
        return signal;
    }

    return recovered;
}

QImage PALBDemodulator::convertToImage(
    const std::vector<float>& videoSignal,
    float brightness,
    float contrast)
{
    QImage image(pixelsPerLine, visibleLines, QImage::Format_Grayscale8);

    size_t expectedSize = pixelsPerLine * visibleLines;

    if (videoSignal.size() < expectedSize) {
        image.fill(Qt::black);
        return image;
    }

    float minVal = *std::min_element(videoSignal.begin(),
                                     videoSignal.begin() + expectedSize);
    float maxVal = *std::max_element(videoSignal.begin(),
                                     videoSignal.begin() + expectedSize);

    if (maxVal == minVal) {
        maxVal = minVal + 1.0f;
    }

    for (int line = 0; line < visibleLines; ++line) {
        uchar* scanLine = image.scanLine(line);

        for (int pixel = 0; pixel < pixelsPerLine; ++pixel) {
            size_t index = line * pixelsPerLine + pixel;

            if (index < videoSignal.size()) {
                float normalized = (videoSignal[index] - minVal) / (maxVal - minVal);
                normalized = (normalized - 0.5f) * contrast + 0.5f + brightness;

                int value = static_cast<int>(normalized * 255.0f);
                value = std::clamp(value, 0, 255);

                scanLine[pixel] = static_cast<uchar>(value);
            } else {
                scanLine[pixel] = 0;
            }
        }
    }

    return image;
}
