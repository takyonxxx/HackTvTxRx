#include "PALDecoder.h"
#include <QDebug>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PALDecoder::PALDecoder(QObject *parent)
    : QObject(parent)
    , m_ncoPhase(0.0)
    , m_ncoPhaseIncrement(0.0)
    , m_videoCarrierOffsetHz(0.0f)
    , m_tuneFrequency(479300000ULL)
    , m_expectedSyncPosition(SAMPLES_PER_LINE)
    , m_samplesSinceSync(0)
    , m_syncConfidence(0.5f)
    , m_currentField(0)
    , m_fieldLineCount(0)
    , m_inVSync(false)
    , m_vSyncCounter(0)
    , m_dcBlockerX1(0.0f)
    , m_dcBlockerY1(0.0f)
    , m_resampleCounter(0)
    , m_currentLine(0)
    , m_samplesInCurrentLine(0)
    , m_agcGain(1.0f)
    , m_peakLevel(0.1f)
    , m_minLevel(-0.1f)
    , m_meanLevel(0.0f)
    , m_videoGain(1.5f)
    , m_videoOffset(0.0f)
    , m_videoInvert(true)
    , m_syncThreshold(-0.2f)
    , m_colorMode(true)
    , m_chromaGain(0.75f)
    , m_totalSamples(0)
    , m_frameCount(0)
    , m_linesProcessed(0)
    , m_syncDetected(0)
    , m_colorPhase(0.0f)
    , m_vPhaseAlternate(false)
    , m_colorCarrierIndex(0)
    , m_burstPhaseError(0.0f)
{
    m_frameBuffer.resize(VIDEO_WIDTH * VIDEO_HEIGHT * 4, 0);
    m_lineBuffer.reserve(SAMPLES_PER_LINE + 50);
    m_lineBufferU.reserve(SAMPLES_PER_LINE + 50);
    m_lineBufferV.reserve(SAMPLES_PER_LINE + 50);

    initFilters();

    // Color carrier sin/cos at DECIMATED rate (5.33 MHz, NOT 6 MHz)
    // Use enough samples for good phase accuracy
    int carrierSamples = static_cast<int>(DECIMATED_RATE / COLOR_CARRIER_FREQ * 100.0f + 0.5f);
    if (carrierSamples < 200) carrierSamples = 200;
    m_colorCarrierSin.resize(carrierSamples);
    m_colorCarrierCos.resize(carrierSamples);
    for (int i = 0; i < carrierSamples; i++) {
        double phase = 2.0 * M_PI * COLOR_CARRIER_FREQ * i / static_cast<double>(DECIMATED_RATE);
        m_colorCarrierSin[i] = static_cast<float>(std::sin(phase));
        m_colorCarrierCos[i] = static_cast<float>(std::cos(phase));
    }

    updateNCO();

    qDebug() << "PAL-B/G Decoder v5 (interlace + improved):";
    qDebug() << "  Decimated rate:" << DECIMATED_RATE / 1e6f << "MHz (16/3)";
    qDebug() << "  Samples/line:" << SAMPLES_PER_LINE;
    qDebug() << "  Blanking:" << BLANKING_SAMPLES << "Active:" << ACTIVE_SAMPLES;
    qDebug() << "  Output:" << VIDEO_WIDTH << "x" << VIDEO_HEIGHT << "(interlaced)";
    qDebug() << "  Color carrier LUT:" << carrierSamples << "samples";
    qDebug() << "  NCO offset:" << m_videoCarrierOffsetHz / 1e6f << "MHz";
}

PALDecoder::~PALDecoder()
{
    float syncRate = m_linesProcessed > 0 ?
                         (m_syncDetected * 100.0f / m_linesProcessed) : 0.0f;
    qDebug() << "PALDecoder: Frames:" << m_frameCount << "Sync rate:" << syncRate << "%";
}

void PALDecoder::setTuneFrequency(uint64_t freqHz)
{
    m_tuneFrequency = freqHz;
    updateNCO();
    qDebug() << "PALDecoder: Tune" << freqHz / 1e6 << "MHz, offset" << m_videoCarrierOffsetHz / 1e6f << "MHz";
}

void PALDecoder::updateNCO()
{
    double tuneMHz = m_tuneFrequency / 1.0e6;
    double videoCarrierMHz;

    if (tuneMHz >= 470.0 && tuneMHz <= 862.0) {
        int ch = static_cast<int>(std::floor((tuneMHz - 470.0 - 0.001) / 8.0));
        if (ch < 0) ch = 0;
        videoCarrierMHz = 470.0 + ch * 8.0 + 1.25;
    } else if (tuneMHz >= 174.0 && tuneMHz <= 230.0) {
        int ch = static_cast<int>(std::floor((tuneMHz - 174.0 - 0.001) / 8.0));
        if (ch < 0) ch = 0;
        videoCarrierMHz = 174.0 + ch * 8.0 + 1.25;
    } else {
        videoCarrierMHz = tuneMHz;
    }

    m_videoCarrierOffsetHz = static_cast<float>((videoCarrierMHz - tuneMHz) * 1.0e6);
    m_ncoPhaseIncrement = -2.0 * M_PI * static_cast<double>(m_videoCarrierOffsetHz)
                          / static_cast<double>(SAMP_RATE);
    m_ncoPhase = 0.0;
}

void PALDecoder::initFilters()
{
    // Video IQ LPF at 16 MHz: cutoff 4.8 MHz (rejects 5.5 MHz audio carrier)
    // Blackman window, more taps for sharper rolloff
    m_videoFilterTaps = designLowPassFIR(4.8e6f, static_cast<float>(SAMP_RATE), 45);

    // Luma LPF at decimated rate: 3.0 MHz (removes chroma subcarrier partially)
    m_lumaFilterTaps = designLowPassFIR(3.0e6f, DECIMATED_RATE, 33);

    // Luma notch filter at 4.43 MHz to remove color carrier from luma
    // Implemented as: luma_clean = luma_lpf (already removes most of it)
    // For now we rely on the 3.0 MHz LPF which attenuates 4.43 MHz well

    // Chroma BPF at decimated rate: center 4.43 MHz, BW 1.2 MHz
    m_chromaFilterTaps = designBandPassFIR(COLOR_CARRIER_FREQ, CHROMA_BANDWIDTH, DECIMATED_RATE, 65);

    qDebug() << "Filters (v5):";
    qDebug() << "  Video IQ LPF:" << m_videoFilterTaps.size() << "taps, 4.8 MHz @ 16 MHz";
    qDebug() << "  Luma LPF:" << m_lumaFilterTaps.size() << "taps, 3.0 MHz @" << DECIMATED_RATE/1e6f << "MHz";
    qDebug() << "  Chroma BPF:" << m_chromaFilterTaps.size() << "taps, 4.43 MHz @" << DECIMATED_RATE/1e6f << "MHz";
}

std::vector<float> PALDecoder::designLowPassFIR(float cutoff, float sampleRate, int numTaps)
{
    std::vector<float> taps(numTaps);
    float fc = cutoff / sampleRate;
    int M = numTaps - 1;

    for (int n = 0; n < numTaps; n++) {
        float mm = n - M / 2.0f;
        float h = (mm == 0.0f) ? 2.0f * fc : std::sin(2.0f * M_PI * fc * mm) / (M_PI * mm);
        // Blackman window for better stopband
        float w = 0.42f - 0.5f * std::cos(2.0f * M_PI * n / M)
                  + 0.08f * std::cos(4.0f * M_PI * n / M);
        taps[n] = h * w;
    }

    float sum = 0.0f;
    for (float tap : taps) sum += tap;
    if (sum != 0.0f) {
        for (float& tap : taps) tap /= sum;
    }
    return taps;
}

std::vector<float> PALDecoder::designBandPassFIR(float centerFreq, float bandwidth,
                                                 float sampleRate, int numTaps)
{
    std::vector<float> taps(numTaps);
    float fc = centerFreq / sampleRate;
    float bw = bandwidth / sampleRate / 2.0f;
    int M = numTaps - 1;

    for (int n = 0; n < numTaps; n++) {
        float mm = n - M / 2.0f;
        float h;
        if (mm == 0.0f) {
            h = 2.0f * bw;
        } else {
            h = (std::sin(2.0f * M_PI * (fc + bw) * mm) -
                 std::sin(2.0f * M_PI * (fc - bw) * mm)) / (M_PI * mm);
        }
        float w = 0.42f - 0.5f * std::cos(2.0f * M_PI * n / M)
                  + 0.08f * std::cos(4.0f * M_PI * n / M);
        taps[n] = h * w * 2.0f * std::cos(2.0f * M_PI * fc * mm);
    }

    float sum = 0.0f;
    for (int i = 0; i < numTaps; i++) {
        float mm = i - (numTaps - 1) / 2.0f;
        sum += taps[i] * std::cos(2.0f * M_PI * fc * mm);
    }
    if (std::abs(sum) > 1e-6f) {
        for (float& tap : taps) tap /= std::abs(sum);
    }
    return taps;
}

std::complex<float> PALDecoder::applyVideoFilter(const std::complex<float>& sample)
{
    m_videoFilterDelay.push_front(sample);
    if (m_videoFilterDelay.size() > m_videoFilterTaps.size())
        m_videoFilterDelay.pop_back();

    std::complex<float> output(0.0f, 0.0f);
    size_t n = std::min(m_videoFilterDelay.size(), m_videoFilterTaps.size());
    for (size_t i = 0; i < n; i++)
        output += m_videoFilterDelay[i] * m_videoFilterTaps[i];
    return output;
}

float PALDecoder::applyLumaFilter(float sample)
{
    m_lumaFilterDelay.push_front(sample);
    if (m_lumaFilterDelay.size() > m_lumaFilterTaps.size())
        m_lumaFilterDelay.pop_back();

    float output = 0.0f;
    size_t n = std::min(m_lumaFilterDelay.size(), m_lumaFilterTaps.size());
    for (size_t i = 0; i < n; i++)
        output += m_lumaFilterDelay[i] * m_lumaFilterTaps[i];
    return output;
}

float PALDecoder::applyLumaNotch(float sample)
{
    // Placeholder - luma LPF at 3.0 MHz already attenuates 4.43 MHz carrier
    return sample;
}

float PALDecoder::applyChromaFilterU(float sample)
{
    m_chromaUFilterDelay.push_front(sample);
    if (m_chromaUFilterDelay.size() > m_chromaFilterTaps.size())
        m_chromaUFilterDelay.pop_back();

    float output = 0.0f;
    size_t n = std::min(m_chromaUFilterDelay.size(), m_chromaFilterTaps.size());
    for (size_t i = 0; i < n; i++)
        output += m_chromaUFilterDelay[i] * m_chromaFilterTaps[i];
    return output;
}

float PALDecoder::applyChromaFilterV(float sample)
{
    m_chromaVFilterDelay.push_front(sample);
    if (m_chromaVFilterDelay.size() > m_chromaFilterTaps.size())
        m_chromaVFilterDelay.pop_back();

    float output = 0.0f;
    size_t n = std::min(m_chromaVFilterDelay.size(), m_chromaFilterTaps.size());
    for (size_t i = 0; i < n; i++)
        output += m_chromaVFilterDelay[i] * m_chromaFilterTaps[i];
    return output;
}

float PALDecoder::dcBlock(float sample)
{
    constexpr float alpha = 0.995f;
    float output = sample - m_dcBlockerX1 + alpha * m_dcBlockerY1;
    m_dcBlockerX1 = sample;
    m_dcBlockerY1 = output;
    return output;
}

float PALDecoder::normalizeAndAGC(float sample)
{
    float absSample = std::abs(sample);

    constexpr float AGC_ATTACK = 0.03f;
    constexpr float AGC_DECAY = 0.9998f;

    if (absSample > m_peakLevel)
        m_peakLevel = m_peakLevel * (1.0f - AGC_ATTACK) + absSample * AGC_ATTACK;
    else
        m_peakLevel *= AGC_DECAY;

    if (sample < m_minLevel)
        m_minLevel = m_minLevel * (1.0f - AGC_ATTACK) + sample * AGC_ATTACK;
    else
        m_minLevel *= AGC_DECAY;

    m_meanLevel = m_meanLevel * 0.9999f + sample * 0.0001f;

    if (m_peakLevel < 0.01f) m_peakLevel = 0.01f;
    if (m_minLevel > -0.01f) m_minLevel = -0.01f;

    float range = m_peakLevel - m_minLevel;
    if (range < 0.05f) range = 0.05f;

    float normalized = 2.0f * (sample - m_minLevel) / range - 1.0f;
    return clipValue(normalized, -1.0f, 1.0f);
}

bool PALDecoder::detectSyncPulse()
{
    if (static_cast<int>(m_sampleHistory.size()) < HSYNC_WIDTH + 10)
        return false;

    int lowCount = 0;
    for (int i = 0; i < HSYNC_WIDTH && i < static_cast<int>(m_sampleHistory.size()); i++) {
        if (m_sampleHistory[i] < m_syncThreshold)
            lowCount++;
    }

    bool afterPulse = true;
    for (int i = HSYNC_WIDTH; i < HSYNC_WIDTH + 10 && i < static_cast<int>(m_sampleHistory.size()); i++) {
        if (m_sampleHistory[i] < m_syncThreshold) {
            afterPulse = false;
            break;
        }
    }

    return (lowCount >= HSYNC_WIDTH * 6 / 10) && afterPulse;
}

void PALDecoder::processSamples(const int8_t* data, size_t len)
{
    if (!data || len == 0) return;
    std::vector<std::complex<float>> samples;
    samples.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        samples.emplace_back(
            static_cast<float>(data[i]) / 128.0f,
            static_cast<float>(data[i + 1]) / 128.0f
            );
    }
    processSamples(samples);
}

void PALDecoder::processSamples(const std::vector<std::complex<float>>& samples)
{
    QMutexLocker locker(&m_processMutex);

    if (samples.empty() || samples.size() > 100000000) return;

    for (const auto& sample : samples) {
        m_totalSamples++;

        if (m_totalSamples % 10000000 == 0) {
            float syncRate = m_linesProcessed > 0 ?
                                 (m_syncDetected * 100.0f / m_linesProcessed) : 0.0f;
            QMetaObject::invokeMethod(this, [this, syncRate]() {
                    emit syncStatsUpdated(syncRate, m_peakLevel, m_minLevel);
                }, Qt::QueuedConnection);
        }

        // NCO frequency shift
        float ncoI = static_cast<float>(std::cos(m_ncoPhase));
        float ncoQ = static_cast<float>(std::sin(m_ncoPhase));
        m_ncoPhase += m_ncoPhaseIncrement;
        if (m_ncoPhase > M_PI) m_ncoPhase -= 2.0 * M_PI;
        else if (m_ncoPhase < -M_PI) m_ncoPhase += 2.0 * M_PI;

        std::complex<float> shifted(
            sample.real() * ncoI - sample.imag() * ncoQ,
            sample.real() * ncoQ + sample.imag() * ncoI
            );

        // Video IQ LPF (4.8 MHz cutoff - rejects audio carrier)
        std::complex<float> filtered = applyVideoFilter(shifted);

        // AM envelope detection
        float magnitude = std::sqrt(filtered.real() * filtered.real() +
                                    filtered.imag() * filtered.imag());
        float dcBlocked = dcBlock(magnitude);
        float normalized = normalizeAndAGC(dcBlocked);

        // Decimate by 3 -> 5.33 MHz
        m_resampleCounter++;
        if (m_resampleCounter < DECIM) continue;
        m_resampleCounter = 0;

        // Luma: LPF at 3.0 MHz (removes chroma carrier)
        float luma = applyLumaFilter(normalized);

        // Chroma demodulation at correct 5.33 MHz rate
        float chromaSin = normalized * m_colorCarrierSin[m_colorCarrierIndex];
        float chromaCos = normalized * m_colorCarrierCos[m_colorCarrierIndex];

        m_colorCarrierIndex++;
        if (m_colorCarrierIndex >= static_cast<int>(m_colorCarrierSin.size()))
            m_colorCarrierIndex = 0;

        float u = applyChromaFilterU(chromaSin) * 2.5f;
        float v = applyChromaFilterV(chromaCos) * 2.5f * (m_vPhaseAlternate ? -1.0f : 1.0f);

        // Sync detection
        m_sampleHistory.push_front(luma);
        if (static_cast<int>(m_sampleHistory.size()) > HISTORY_SIZE)
            m_sampleHistory.pop_back();

        processVideoSample(luma);

        // Store chroma for active area only
        if (m_samplesInCurrentLine > BLANKING_SAMPLES &&
            static_cast<int>(m_lineBufferU.size()) < ACTIVE_SAMPLES) {
            m_lineBufferU.push_back(u);
            m_lineBufferV.push_back(v);
        }
    }
}

void PALDecoder::processVideoSample(float sample)
{
    m_samplesInCurrentLine++;
    m_samplesSinceSync++;

    bool inSyncWindow = (m_samplesSinceSync >= m_expectedSyncPosition - SYNC_SEARCH_WINDOW) &&
                        (m_samplesSinceSync <= m_expectedSyncPosition + SYNC_SEARCH_WINDOW);

    if (inSyncWindow && detectSyncPulse()) {
        int error = m_samplesSinceSync - m_expectedSyncPosition;
        m_expectedSyncPosition += error / 8;
        m_expectedSyncPosition = std::max(SAMPLES_PER_LINE - 20,
                                          std::min(SAMPLES_PER_LINE + 20, m_expectedSyncPosition));

        m_syncConfidence = std::min(1.0f, m_syncConfidence + 0.1f);
        m_syncDetected++;

        finalizeLine();
        m_samplesSinceSync = 0;
        m_samplesInCurrentLine = 0;
        m_vPhaseAlternate = !m_vPhaseAlternate;
        m_colorCarrierIndex = 0;
        return;
    }

    if (m_samplesSinceSync >= m_expectedSyncPosition + SYNC_SEARCH_WINDOW + 40) {
        m_syncConfidence = std::max(0.0f, m_syncConfidence - 0.05f);
        finalizeLine();
        m_samplesSinceSync = 0;
        m_samplesInCurrentLine = 0;
        m_vPhaseAlternate = !m_vPhaseAlternate;
        m_colorCarrierIndex = 0;
        return;
    }

    // Collect active video (after blanking)
    if (m_samplesInCurrentLine > BLANKING_SAMPLES &&
        static_cast<int>(m_lineBuffer.size()) < ACTIVE_SAMPLES) {
        m_lineBuffer.push_back(sample);
    }
}

void PALDecoder::finalizeLine()
{
    m_linesProcessed++;
    m_currentLine++;

    // Determine which field we're in and the display Y position
    // Field 1 (lines 1-312): display on even rows (0, 2, 4, ...)
    // Field 2 (lines 313-625): display on odd rows (1, 3, 5, ...)
    int displayY = -1;

    if (m_currentLine >= FIRST_VISIBLE_LINE_F1 &&
        m_currentLine < FIRST_VISIBLE_LINE_F1 + VISIBLE_LINES_PER_FIELD) {
        // Field 1 - even rows
        int fieldLine = m_currentLine - FIRST_VISIBLE_LINE_F1;
        displayY = fieldLine * 2;  // 0, 2, 4, ...
    }
    else if (m_currentLine >= FIRST_VISIBLE_LINE_F2 &&
             m_currentLine < FIRST_VISIBLE_LINE_F2 + VISIBLE_LINES_PER_FIELD) {
        // Field 2 - odd rows
        int fieldLine = m_currentLine - FIRST_VISIBLE_LINE_F2;
        displayY = fieldLine * 2 + 1;  // 1, 3, 5, ...
    }

    if (displayY >= 0 && displayY < VIDEO_HEIGHT) {
        int samplesToUse = std::min(static_cast<int>(m_lineBuffer.size()), ACTIVE_SAMPLES);

        std::vector<float> currentLineU(VIDEO_WIDTH, 0.0f);
        std::vector<float> currentLineV(VIDEO_WIDTH, 0.0f);

        for (int x = 0; x < VIDEO_WIDTH; x++) {
            float srcX = (x * samplesToUse) / static_cast<float>(VIDEO_WIDTH);
            int idx = static_cast<int>(srcX);
            float frac = srcX - idx;

            uint8_t r, g, b;
            if (idx >= samplesToUse || samplesToUse < 10) {
                r = g = b = 0;
            } else {
                // Luma with linear interpolation
                float Y0 = (m_lineBuffer[idx] + 1.0f) * 0.5f;
                float Y1 = (idx + 1 < samplesToUse) ?
                               (m_lineBuffer[idx + 1] + 1.0f) * 0.5f : Y0;
                float Y = Y0 + (Y1 - Y0) * frac;
                Y = Y * m_videoGain + m_videoOffset;
                Y = clipValue(Y, 0.0f, 1.0f);

                float U = 0.0f, V = 0.0f;

                if (m_colorMode && idx < static_cast<int>(m_lineBufferU.size())) {
                    int idx2 = std::min(idx + 1, static_cast<int>(m_lineBufferU.size()) - 1);
                    U = m_lineBufferU[idx] + (m_lineBufferU[idx2] - m_lineBufferU[idx]) * frac;
                    V = m_lineBufferV[idx] + (m_lineBufferV[idx2] - m_lineBufferV[idx]) * frac;

                    currentLineU[x] = U;
                    currentLineV[x] = V;

                    // PAL comb filter
                    if (!m_prevLineU.empty() && x < static_cast<int>(m_prevLineU.size())) {
                        U = (U + m_prevLineU[x]) * 0.5f;
                        V = (V - m_prevLineV[x]) * 0.5f;
                    }

                    U *= m_chromaGain;
                    V *= m_chromaGain;
                }

                yuv2rgb(Y, U, V, r, g, b);

                if (m_videoInvert) {
                    r = 255 - r;
                    g = 255 - g;
                    b = 255 - b;
                }
            }

            int offset = (displayY * VIDEO_WIDTH + x) * 4;
            m_frameBuffer[offset + 0] = b;
            m_frameBuffer[offset + 1] = g;
            m_frameBuffer[offset + 2] = r;
            m_frameBuffer[offset + 3] = 255;
        }

        m_prevLineU = currentLineU;
        m_prevLineV = currentLineV;
    }

    m_lineBuffer.clear();
    m_lineBufferU.clear();
    m_lineBufferV.clear();

    // End of frame: emit after 625 lines
    if (m_currentLine >= LINES_PER_FRAME) {
        buildFrame();
        m_currentLine = 0;
        m_prevLineU.clear();
        m_prevLineV.clear();
    }
}

void PALDecoder::buildFrame()
{
    m_frameCount++;

    QImage frame(VIDEO_WIDTH, VIDEO_HEIGHT, QImage::Format_RGB32);
    for (int y = 0; y < VIDEO_HEIGHT; y++) {
        uint8_t* scanLine = frame.scanLine(y);
        std::memcpy(scanLine, &m_frameBuffer[y * VIDEO_WIDTH * 4], VIDEO_WIDTH * 4);
    }

    emit frameReady(frame);
}

QImage PALDecoder::getCurrentFrame() const
{
    QImage frame(VIDEO_WIDTH, VIDEO_HEIGHT, QImage::Format_RGB32);
    for (int y = 0; y < VIDEO_HEIGHT; y++) {
        uint8_t* scanLine = frame.scanLine(y);
        std::memcpy(scanLine, &m_frameBuffer[y * VIDEO_WIDTH * 4], VIDEO_WIDTH * 4);
    }
    return frame;
}

float PALDecoder::clipValue(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

void PALDecoder::yuv2rgb(float y, float u, float v, uint8_t& r, uint8_t& g, uint8_t& b)
{
    // BT.601
    float rf = y + 1.140f * v;
    float gf = y - 0.396f * u - 0.581f * v;
    float bf = y + 2.029f * u;

    r = static_cast<uint8_t>(clipValue(rf * 255.0f, 0.0f, 255.0f));
    g = static_cast<uint8_t>(clipValue(gf * 255.0f, 0.0f, 255.0f));
    b = static_cast<uint8_t>(clipValue(bf * 255.0f, 0.0f, 255.0f));
}
