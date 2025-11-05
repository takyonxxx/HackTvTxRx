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
    , m_totalSamples(0)
    , m_frameCount(0)
    , m_linesProcessed(0)
    , m_syncDetected(0)
{
    m_resampleDecim = 3;

    m_frameBuffer.resize(VIDEO_WIDTH * VIDEO_HEIGHT, 128);
    m_lineBuffer.reserve(SAMPLES_PER_LINE + 100);

    initFilters();

    qDebug() << "PAL-B/G Decoder (PLL-BASED SYNC + AUDIO):";
    qDebug() << "  625 lines, 25 fps, AM demodulation";
    qDebug() << "  Audio: 48 kHz, FM demodulation, 60ms buffer";
    qDebug() << "  Resolution:" << VIDEO_WIDTH << "x" << VIDEO_HEIGHT;
    qDebug() << "  Default sync threshold:" << m_syncThreshold;
}

PALDecoder::~PALDecoder()
{
    float syncRate = m_linesProcessed > 0 ?
                         (m_syncDetected * 100.0f / m_linesProcessed) : 0.0f;
    qDebug() << "PALDecoder: Frames:" << m_frameCount << "Sync rate:" << syncRate << "%";
}

void PALDecoder::initFilters()
{
    m_videoFilterTaps = designLowPassFIR(5.0e6f, SAMP_RATE, 33);
    m_lumaFilterTaps = designLowPassFIR(3.0e6f, VIDEO_SAMP_RATE, 33);
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

// 0.98 = hızlı DC blocking
// 0.995 = yavaş DC blocking
// TEST: 0.95, 0.98, 0.99, 0.995
float PALDecoder::dcBlock(float sample)
{
    constexpr float alpha = 0.98f;
    float output = sample - m_dcBlockerX1 + alpha * m_dcBlockerY1;
    m_dcBlockerX1 = sample;
    m_dcBlockerY1 = output;
    return output;
}

// Attack: 0.05 = hızlı, 0.01 = yavaş
// Decay: 0.9995 = yavaş, 0.999 = hızlı
// TEST: Attack: 0.01-0.1, Decay: 0.999-0.9999

float PALDecoder::normalizeAndAGC(float sample)
{
    float absSample = std::abs(sample);

    constexpr float AGC_ATTACK = 0.05f;  // <<<< DEĞİŞTİR (0.01 - 0.1)
    constexpr float AGC_DECAY = 0.999f; // <<<< DEĞİŞTİR (0.999 - 0.9999)

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

    // Ensure valid range
    if (m_peakLevel < 0.01f) m_peakLevel = 0.01f;
    if (m_minLevel > -0.01f) m_minLevel = -0.01f;

    // Normalize to -1..+1 range
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

    // Use adjustable threshold (not constexpr anymore)
    float SYNC_THRESHOLD = m_syncThreshold; // Use member variable

    // Count low samples
    int lowCount = 0;
    for (int i = 0; i < HSYNC_WIDTH && i < static_cast<int>(m_sampleHistory.size()); i++) {
        if (m_sampleHistory[i] < SYNC_THRESHOLD) {
            lowCount++;
        }
    }

    // Check for end of sync (rising edge)
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
    for (const auto& sample : samples) {
        m_totalSamples++;

        // Stats every 10M samples
        if (m_totalSamples % 10000000 == 0) {
            float syncRate = m_linesProcessed > 0 ?
                                 (m_syncDetected * 100.0f / m_linesProcessed) : 0.0f;
            emit syncStatsUpdated(syncRate, m_peakLevel, m_minLevel);
        }

        // VIDEO PROCESSING CHAIN
        std::complex<float> filtered = applyVideoFilter(sample);
        float magnitude = std::sqrt(filtered.real() * filtered.real() +
                                    filtered.imag() * filtered.imag());
        float dcBlocked = dcBlock(magnitude);
        float normalized = normalizeAndAGC(dcBlocked);

        // Video decimation to 6 MHz
        m_resampleCounter++;
        if (m_resampleCounter >= m_resampleDecim) {
            m_resampleCounter = 0;
            float luma = applyLumaFilter(normalized);

            m_sampleHistory.push_front(luma);
            if (m_sampleHistory.size() > HISTORY_SIZE) {
                m_sampleHistory.pop_back();
            }

            processVideoSample(luma);
        }
    }
}

void PALDecoder::processVideoSample(float sample)
{
    m_samplesInCurrentLine++;
    m_samplesSinceSync++;

    // PLL: Check if we're near expected sync position
    bool inSyncWindow = (m_samplesSinceSync >= m_expectedSyncPosition - SYNC_SEARCH_WINDOW) &&
                        (m_samplesSinceSync <= m_expectedSyncPosition + SYNC_SEARCH_WINDOW);

    if (inSyncWindow && detectSyncPulse()) {
        // Sync found! Adjust PLL
        int error = m_samplesSinceSync - m_expectedSyncPosition;
        m_expectedSyncPosition += error / 16; // Slow correction

        // Clamp expected position
        m_expectedSyncPosition = std::max(SAMPLES_PER_LINE - 20,
                                          std::min(SAMPLES_PER_LINE + 20, m_expectedSyncPosition));

        m_syncConfidence = std::min(1.0f, m_syncConfidence + 0.1f);
        m_syncDetected++;

        // End current line, start new one
        finalizeLine();
        m_samplesSinceSync = 0;
        m_samplesInCurrentLine = 0;
        return;
    }

    // Timeout: force new line even without sync
    if (m_samplesSinceSync >= m_expectedSyncPosition + SYNC_SEARCH_WINDOW + 50) {
        m_syncConfidence = std::max(0.0f, m_syncConfidence - 0.05f);
        finalizeLine();
        m_samplesSinceSync = 0;
        m_samplesInCurrentLine = 0;
        return;
    }

    // Collect video data (skip first ~40 samples after sync for back porch)
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

        // Map 576 lines to 384 height (decimate by 1.5)
        int y = (lineIndex * VIDEO_HEIGHT) / VISIBLE_LINES; // 0-383

        // Map 384 samples to 576 width (interpolate by 1.5)
        int samplesToUse = std::min(static_cast<int>(m_lineBuffer.size()), SAMPLES_PER_LINE);

        for (int x = 0; x < VIDEO_WIDTH; x++) {
            // Map x (0-575) to sample index (0-383)
            float srcX = (x * SAMPLES_PER_LINE) / (float)VIDEO_WIDTH;
            int idx = static_cast<int>(srcX);

            if (idx >= samplesToUse) {
                m_frameBuffer[y * VIDEO_WIDTH + x] = m_videoInvert ? 255 : 0;
                continue;
            }

            float value = (m_lineBuffer[idx] + 1.0f) * 0.5f;
            value = value * m_videoGain + m_videoOffset;
            value = clipValue(value, 0.0f, 1.0f);

            if (m_videoInvert) {
                value = 1.0f - value;
            }

            m_frameBuffer[y * VIDEO_WIDTH + x] = static_cast<uint8_t>(value * 255.0f);
        }
    }

    m_lineBuffer.clear();

    if (m_currentLine >= LINES_PER_FRAME) {
        buildFrame();
        m_currentLine = 0;
    }
}

void PALDecoder::buildFrame()
{
    // m_frameCount++;

    // if (m_frameCount % 50 == 0) {
    //     float syncRate = m_linesProcessed > 0 ?
    //                          (m_syncDetected * 100.0f / m_linesProcessed) : 0.0f;
    //     qDebug() << "Frame" << m_frameCount
    //              << "| Sync:" << QString::number(syncRate, 'f', 1) << "%"
    //              << "| Confidence:" << QString::number(m_syncConfidence, 'f', 2);
    // }

    QImage frame(VIDEO_WIDTH, VIDEO_HEIGHT, QImage::Format_Grayscale8);

    for (int y = 0; y < VIDEO_HEIGHT; y++) {
        uint8_t* scanLine = frame.scanLine(y);
        std::memcpy(scanLine, &m_frameBuffer[y * VIDEO_WIDTH], VIDEO_WIDTH);
    }

    emit frameReady(frame);
}

QImage PALDecoder::getCurrentFrame() const
{
    QImage frame(VIDEO_WIDTH, VIDEO_HEIGHT, QImage::Format_Grayscale8);

    for (int y = 0; y < VIDEO_HEIGHT; y++) {
        uint8_t* scanLine = frame.scanLine(y);
        std::memcpy(scanLine, &m_frameBuffer[y * VIDEO_WIDTH], VIDEO_WIDTH);
    }

    return frame;
}

float PALDecoder::clipValue(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}
