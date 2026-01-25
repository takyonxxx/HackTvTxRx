#include "PALDecoder.h"
#include <QDebug>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PALDecoder::PALDecoder(QObject *parent)
    : QObject(parent)
    , m_expectedSyncPosition(SAMPLES_PER_LINE)
    , m_samplesSinceSync(0)
    , m_syncConfidence(0.5f)
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
    , m_videoInvert(false)
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
    m_resampleDecim = 3;

    m_frameBuffer.resize(VIDEO_WIDTH * VIDEO_HEIGHT * 4, 128);
    m_lineBuffer.reserve(SAMPLES_PER_LINE + 100);
    m_lineBufferU.reserve(SAMPLES_PER_LINE + 100);
    m_lineBufferV.reserve(SAMPLES_PER_LINE + 100);

    initFilters();

    int carrierSamples = (int)(VIDEO_SAMP_RATE / COLOR_CARRIER_FREQ * 10);
    m_colorCarrierSin.resize(carrierSamples);
    m_colorCarrierCos.resize(carrierSamples);
    for (int i = 0; i < carrierSamples; i++) {
        float phase = 2.0f * M_PI * COLOR_CARRIER_FREQ * i / VIDEO_SAMP_RATE;
        m_colorCarrierSin[i] = std::sin(phase);
        m_colorCarrierCos[i] = std::cos(phase);
    }

    qDebug() << "PAL-B/G Color Decoder:";
    qDebug() << "  625 lines, 25 fps, Color subcarrier:" << COLOR_CARRIER_FREQ << "Hz";
    qDebug() << "  Resolution:" << VIDEO_WIDTH << "x" << VIDEO_HEIGHT;
    qDebug() << "  PAL Comb Filter: ENABLED";
}

PALDecoder::~PALDecoder()
{
    float syncRate = m_linesProcessed > 0 ?
                         (m_syncDetected * 100.0f / m_linesProcessed) : 0.0f;
    qDebug() << "PALDecoder: Frames:" << m_frameCount << "Sync rate:" << syncRate << "%";
}

void PALDecoder::initFilters()
{
    m_videoFilterTaps = designLowPassFIR(5.5e6f, SAMP_RATE, 33);
    m_lumaFilterTaps = designLowPassFIR(3.2e6f, VIDEO_SAMP_RATE, 33);
    m_chromaFilterTaps = designBandPassFIR(COLOR_CARRIER_FREQ, CHROMA_BANDWIDTH, VIDEO_SAMP_RATE, 65);
}

std::vector<float> PALDecoder::designLowPassFIR(float cutoff, float sampleRate, int numTaps)
{
    std::vector<float> taps(numTaps);
    float fc = cutoff / sampleRate;
    int M = numTaps - 1;

    for (int n = 0; n < numTaps; n++) {
        float mm = n - M / 2.0f;
        float h = (mm == 0.0f) ? 2.0f * fc : std::sin(2.0f * M_PI * fc * mm) / (M_PI * mm);
        float w = 0.54f - 0.46f * std::cos(2.0f * M_PI * n / M);
        taps[n] = h * w;
    }

    float sum = 0.0f;
    for (float tap : taps) sum += tap;
    if (sum != 0.0f) {
        for (float& tap : taps) tap /= sum;
    }

    return taps;
}

std::vector<float> PALDecoder::designBandPassFIR(float centerFreq, float bandwidth, float sampleRate, int numTaps)
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
        float w = 0.54f - 0.46f * std::cos(2.0f * M_PI * n / M);
        taps[n] = h * w * 2.0f * std::cos(2.0f * M_PI * fc * mm);
    }

    float sum = 0.0f;
    for (int i = 0; i < numTaps; i += 2) sum += taps[i];
    if (sum != 0.0f) {
        for (float& tap : taps) tap /= sum;
    }

    return taps;
}

std::complex<float> PALDecoder::applyVideoFilter(const std::complex<float>& sample)
{
    m_videoFilterDelay.push_front(sample);
    if (m_videoFilterDelay.size() > m_videoFilterTaps.size()) {
        m_videoFilterDelay.pop_back();
    }

    std::complex<float> output(0.0f, 0.0f);
    size_t n = std::min(m_videoFilterDelay.size(), m_videoFilterTaps.size());
    for (size_t i = 0; i < n; i++) {
        output += m_videoFilterDelay[i] * m_videoFilterTaps[i];
    }
    return output;
}

float PALDecoder::applyLumaFilter(float sample)
{
    m_lumaFilterDelay.push_front(sample);
    if (m_lumaFilterDelay.size() > m_lumaFilterTaps.size()) {
        m_lumaFilterDelay.pop_back();
    }

    float output = 0.0f;
    size_t n = std::min(m_lumaFilterDelay.size(), m_lumaFilterTaps.size());
    for (size_t i = 0; i < n; i++) {
        output += m_lumaFilterDelay[i] * m_lumaFilterTaps[i];
    }
    return output;
}

float PALDecoder::applyChromaFilterU(float sample)
{
    m_chromaUFilterDelay.push_front(sample);
    if (m_chromaUFilterDelay.size() > m_chromaFilterTaps.size()) {
        m_chromaUFilterDelay.pop_back();
    }

    float output = 0.0f;
    size_t n = std::min(m_chromaUFilterDelay.size(), m_chromaFilterTaps.size());
    for (size_t i = 0; i < n; i++) {
        output += m_chromaUFilterDelay[i] * m_chromaFilterTaps[i];
    }
    return output;
}

float PALDecoder::applyChromaFilterV(float sample)
{
    m_chromaVFilterDelay.push_front(sample);
    if (m_chromaVFilterDelay.size() > m_chromaFilterTaps.size()) {
        m_chromaVFilterDelay.pop_back();
    }

    float output = 0.0f;
    size_t n = std::min(m_chromaVFilterDelay.size(), m_chromaFilterTaps.size());
    for (size_t i = 0; i < n; i++) {
        output += m_chromaVFilterDelay[i] * m_chromaFilterTaps[i];
    }
    return output;
}

float PALDecoder::dcBlock(float sample)
{
    constexpr float alpha = 0.98f;
    float output = sample - m_dcBlockerX1 + alpha * m_dcBlockerY1;
    m_dcBlockerX1 = sample;
    m_dcBlockerY1 = output;
    return output;
}

float PALDecoder::normalizeAndAGC(float sample)
{
    float absSample = std::abs(sample);

    constexpr float AGC_ATTACK = 0.05f;
    constexpr float AGC_DECAY = 0.999f;

    if (absSample > m_peakLevel) {
        m_peakLevel = m_peakLevel * (1.0f - AGC_ATTACK) + absSample * AGC_ATTACK;
    } else {
        m_peakLevel *= AGC_DECAY;
    }

    if (sample < m_minLevel) {
        m_minLevel = m_minLevel * (1.0f - AGC_ATTACK) + sample * AGC_ATTACK;
    } else {
        m_minLevel *= AGC_DECAY;
    }

    m_meanLevel = m_meanLevel * 0.999f + sample * 0.001f;

    if (m_peakLevel < 0.01f) m_peakLevel = 0.01f;
    if (m_minLevel > -0.01f) m_minLevel = -0.01f;

    float range = m_peakLevel - m_minLevel;
    if (range < 0.1f) range = 0.1f;

    float normalized = 2.0f * (sample - m_minLevel) / range - 1.0f;

    return clipValue(normalized, -1.0f, 1.0f);
}

bool PALDecoder::detectSyncPulse()
{
    if (m_sampleHistory.size() < HSYNC_WIDTH + 10) {
        return false;
    }

    float SYNC_THRESHOLD = m_syncThreshold;

    int lowCount = 0;
    for (int i = 0; i < HSYNC_WIDTH && i < static_cast<int>(m_sampleHistory.size()); i++) {
        if (m_sampleHistory[i] < SYNC_THRESHOLD) {
            lowCount++;
        }
    }

    bool afterPulse = true;
    for (int i = HSYNC_WIDTH; i < HSYNC_WIDTH + 10 && i < static_cast<int>(m_sampleHistory.size()); i++) {
        if (m_sampleHistory[i] < SYNC_THRESHOLD) {
            afterPulse = false;
            break;
        }
    }

    return (lowCount >= HSYNC_WIDTH / 2) && afterPulse;
}

void PALDecoder::processSamples(const int8_t* data, size_t len)
{
    if (!data || len == 0) return;

    std::vector<std::complex<float>> samples;
    samples.reserve(len / 2);

    for (size_t i = 0; i < len; i += 2) {
        float I = static_cast<float>(data[i]) / 128.0f;
        float Q = static_cast<float>(data[i + 1]) / 128.0f;
        samples.emplace_back(I, Q);
    }

    processSamples(samples);
}

void PALDecoder::processSamples(const std::vector<std::complex<float>>& samples)
{
    QMutexLocker locker(&m_processMutex);

    if (samples.empty()) {
        qWarning() << "PALDecoder: Empty samples vector!";
        return;
    }

    if (samples.size() > 100000000) {
        qWarning() << "PALDecoder: Suspiciously large samples:" << samples.size();
        return;
    }

    for (const auto& sample : samples) {
        m_totalSamples++;

        if (m_totalSamples % 10000000 == 0) {
            float syncRate = m_linesProcessed > 0 ?
                                 (m_syncDetected * 100.0f / m_linesProcessed) : 0.0f;
            QMetaObject::invokeMethod(this, [this, syncRate]() {
                    emit syncStatsUpdated(syncRate, m_peakLevel, m_minLevel);
                }, Qt::QueuedConnection);
        }

        std::complex<float> filtered = applyVideoFilter(sample);
        float magnitude = std::sqrt(filtered.real() * filtered.real() +
                                    filtered.imag() * filtered.imag());
        float dcBlocked = dcBlock(magnitude);
        float normalized = normalizeAndAGC(dcBlocked);

        m_resampleCounter++;
        if (m_resampleCounter >= m_resampleDecim) {
            m_resampleCounter = 0;
            float luma = applyLumaFilter(normalized);

            float chromaSin = normalized * m_colorCarrierSin[m_colorCarrierIndex];
            float chromaCos = normalized * m_colorCarrierCos[m_colorCarrierIndex];
            
            m_colorCarrierIndex++;
            if (m_colorCarrierIndex >= (int)m_colorCarrierSin.size()) {
                m_colorCarrierIndex = 0;
            }

            float u = applyChromaFilterU(chromaSin) * 2.5f;
            float v = applyChromaFilterV(chromaCos) * 2.5f * (m_vPhaseAlternate ? -1.0f : 1.0f);

            m_sampleHistory.push_front(luma);
            if (m_sampleHistory.size() > HISTORY_SIZE) {
                m_sampleHistory.pop_back();
            }

            processVideoSample(luma);
            
            if (m_samplesInCurrentLine > 40 && m_lineBuffer.size() < SAMPLES_PER_LINE) {
                m_lineBufferU.push_back(u);
                m_lineBufferV.push_back(v);
            }
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
        m_expectedSyncPosition += error / 16;

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

    if (m_samplesSinceSync >= m_expectedSyncPosition + SYNC_SEARCH_WINDOW + 50) {
        m_syncConfidence = std::max(0.0f, m_syncConfidence - 0.05f);
        finalizeLine();
        m_samplesSinceSync = 0;
        m_samplesInCurrentLine = 0;
        m_vPhaseAlternate = !m_vPhaseAlternate;
        m_colorCarrierIndex = 0;
        return;
    }

    if (m_samplesInCurrentLine > 40 && m_lineBuffer.size() < SAMPLES_PER_LINE) {
        m_lineBuffer.push_back(sample);
    }
}

void PALDecoder::finalizeLine()
{
    m_linesProcessed++;
    m_currentLine++;

    if (m_currentLine >= FIRST_VISIBLE_LINE &&
        m_currentLine < FIRST_VISIBLE_LINE + VISIBLE_LINES) {

        int lineIndex = m_currentLine - FIRST_VISIBLE_LINE;
        int y = (lineIndex * VIDEO_HEIGHT) / VISIBLE_LINES;

        int samplesToUse = std::min(static_cast<int>(m_lineBuffer.size()), SAMPLES_PER_LINE);
        
        // Geçici U/V buffer (comb filter için)
        std::vector<float> currentLineU(VIDEO_WIDTH, 0.0f);
        std::vector<float> currentLineV(VIDEO_WIDTH, 0.0f);

        for (int x = 0; x < VIDEO_WIDTH; x++) {
            float srcX = (x * SAMPLES_PER_LINE) / (float)VIDEO_WIDTH;
            int idx = static_cast<int>(srcX);
            float frac = srcX - idx;

            uint8_t r, g, b;
            if (idx >= samplesToUse) {
                r = g = b = m_videoInvert ? 255 : 0;
            } else {
                // Luma
                float Y = (m_lineBuffer[idx] + 1.0f) * 0.5f;
                Y = Y * m_videoGain + m_videoOffset;
                Y = clipValue(Y, 0.0f, 1.0f);

                float U = 0.0f, V = 0.0f;
                
                if (m_colorMode && idx < (int)m_lineBufferU.size()) {
                    // Bilinear interpolasyon
                    int idx2 = std::min(idx + 1, (int)m_lineBufferU.size() - 1);
                    
                    float U1 = m_lineBufferU[idx];
                    float U2 = m_lineBufferU[idx2];
                    float V1 = m_lineBufferV[idx];
                    float V2 = m_lineBufferV[idx2];
                    
                    U = U1 + (U2 - U1) * frac;
                    V = V1 + (V2 - V1) * frac;
                    
                    // Önce currentLine'a kaydet (chromaGain uygulanmadan)
                    currentLineU[x] = U;
                    currentLineV[x] = V;
                    
                    // PAL Delay Line (Comb Filter)
                    if (!m_prevLineU.empty() && x < (int)m_prevLineU.size()) {
                        U = (U + m_prevLineU[x]) * 0.5f;
                        V = (V - m_prevLineV[x]) * 0.5f;
                    }
                    
                    // Chroma gain uygula (comb filter'dan SONRA)
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

            int offset = (y * VIDEO_WIDTH + x) * 4;
            m_frameBuffer[offset + 0] = b;
            m_frameBuffer[offset + 1] = g;
            m_frameBuffer[offset + 2] = r;
            m_frameBuffer[offset + 3] = 255;
        }
        
        // Bir sonraki satır için sakla
        m_prevLineU = currentLineU;
        m_prevLineV = currentLineV;
    }

    m_lineBuffer.clear();
    m_lineBufferU.clear();
    m_lineBufferV.clear();

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
    float rf = y + 1.140f * v;
    float gf = y - 0.396f * u - 0.581f * v;
    float bf = y + 2.029f * u;

    r = static_cast<uint8_t>(clipValue(rf * 255.0f, 0.0f, 255.0f));
    g = static_cast<uint8_t>(clipValue(gf * 255.0f, 0.0f, 255.0f));
    b = static_cast<uint8_t>(clipValue(bf * 255.0f, 0.0f, 255.0f));
}
