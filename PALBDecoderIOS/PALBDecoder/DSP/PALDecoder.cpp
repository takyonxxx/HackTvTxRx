#include "PALDecoder.h"
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PALDecoder::PALDecoder()
    : m_sampleRate(16000000)
    , m_decimFactor(2)
    , m_decimatedRate(8000000.0f)
    , m_chromaBandwidth(1.2e6f)
    , m_ncoPhase(0.0)
    , m_ncoPhaseIncrement(0.0)
    , m_videoCarrierOffsetHz(0.0f)
    , m_tuneFrequency(479300000ULL)
    , m_samplesPerLine(1024)
    , m_samplesPerLineFrac(0.0f)
    , m_sampleOffset(0)
    , m_sampleOffsetFrac(0.0f)
    , m_sampleOffsetDetected(0)
    , m_hSyncShift(0.0f)
    , m_hSyncErrorCount(0)
    , m_prevSample(0.0f)
    , m_numberSamplesPerHTop(0)
    , m_numberSamplesPerHSync(0)
    , m_numberSamplesPerLineSignals(0)
    , m_numberSamplesHSyncCrop(0)
    , m_lineIndex(0)
    , m_fieldIndex(0)
    , m_fieldDetectStartPos(0)
    , m_fieldDetectEndPos(0)
    , m_vSyncDetectStartPos(0)
    , m_vSyncDetectEndPos(0)
    , m_fieldDetectSampleCount(0)
    , m_vSyncDetectSampleCount(0)
    , m_vSyncDetectThreshold(0)
    , m_fieldDetectThreshold1(0)
    , m_fieldDetectThreshold2(0)
    , m_dcBlockerX1(0.0f)
    , m_dcBlockerY1(0.0f)
    , m_resampleCounter(0)
    , m_chromaUAccum(0.0f)
    , m_chromaVAccum(0.0f)
    , m_ampMin(-1.0f)
    , m_ampMax(1.0f)
    , m_ampDelta(2.0f)
    , m_effMin(20.0f)
    , m_effMax(-20.0f)
    , m_amSampleIndex(0)
    , m_videoGain(1.5f)
    , m_videoOffset(0.0f)
    , m_videoInvert(true)
    , m_syncLevel(0.0f)
    , m_colorMode(false)
    , m_chromaGain(0.75f)
    , m_hSyncEnabled(true)
    , m_vSyncEnabled(true)
    , m_totalSamples(0)
    , m_frameCount(0)
    , m_linesProcessed(0)
    , m_syncDetected(0)
    , m_syncQualityWindow(0)
    , m_syncFoundInWindow(0)
    , m_syncErrorAccum(0.0)
    , m_lastSyncQuality(0.0f)
    , m_vPhaseAlternate(false)
    , m_colorCarrierIndex(0)
{
    m_frameBuffer.resize(VIDEO_WIDTH * VIDEO_HEIGHT * 4, 0);
    m_lineBuffer.reserve(2048);
    m_lineBufferU.reserve(2048);
    m_lineBufferV.reserve(2048);
    setSampleRate(16000000);
    updateNCO();
}

PALDecoder::~PALDecoder() {}

void PALDecoder::setSampleRate(int sampleRate)
{
    std::lock_guard<std::mutex> lock(m_processMutex);
    m_sampleRate = sampleRate;
    if (m_sampleRate <= 12500000) m_decimFactor = 1;
    else m_decimFactor = 2;
    m_decimatedRate = static_cast<float>(m_sampleRate) / m_decimFactor;
    if (m_sampleRate >= 16000000) m_chromaBandwidth = 1.5e6f;
    else m_chromaBandwidth = 1.2e6f;
    applyStandard();
    initFilters();
    rebuildColorLUT();
    // NCO phase increment depends on sample rate - must recalculate
    m_ncoPhaseIncrement = -2.0 * M_PI * static_cast<double>(m_videoCarrierOffsetHz)
                          / static_cast<double>(m_sampleRate);
    m_ncoPhase = 0.0;
    m_resampleCounter = 0;
    m_sampleOffset = 0;
    m_sampleOffsetFrac = 0.0f;
    m_hSyncShift = 0.0f;
    m_hSyncErrorCount = 0;
    m_lineIndex = 0;
    m_fieldIndex = 0;
    m_colorCarrierIndex = 0;
    m_ampMin = -1.0f; m_ampMax = 1.0f; m_ampDelta = 2.0f;
    m_effMin = 20.0f; m_effMax = -20.0f; m_amSampleIndex = 0;
    m_videoFilterDelay.clear();
    m_lumaFilterDelay.clear();
    m_chromaUFilterDelay.clear();
    m_chromaVFilterDelay.clear();
}

void PALDecoder::applyStandard()
{
    float rate = static_cast<float>(m_sampleRate);
    float exactSPL = rate / (NB_LINES * FPS);
    m_samplesPerLine = static_cast<int>(exactSPL);
    m_samplesPerLineFrac = exactSPL - m_samplesPerLine;
    m_numberSamplesPerHTop        = static_cast<int>(SYNC_PULSE_FRAC * exactSPL);
    m_numberSamplesPerHSync       = static_cast<int>(HSYNC_FRAC * exactSPL);
    m_numberSamplesPerLineSignals = static_cast<int>(BLANKING_FRAC * exactSPL);
    m_numberSamplesHSyncCrop      = static_cast<int>(HSYNC_CROP_FRAC * exactSPL);
    m_fieldDetectStartPos = static_cast<int>(FIELD_DETECT_START * exactSPL);
    m_fieldDetectEndPos   = static_cast<int>(FIELD_DETECT_END * exactSPL);
    m_vSyncDetectStartPos = static_cast<int>((FIELD_DETECT_START + HALF_LINE) * exactSPL);
    m_vSyncDetectEndPos   = static_cast<int>((FIELD_DETECT_END + HALF_LINE) * exactSPL);
    float detectTotalLen = (FIELD_DETECT_END - FIELD_DETECT_START) * exactSPL;
    m_fieldDetectThreshold1 = static_cast<int>(detectTotalLen * 0.75f);
    m_fieldDetectThreshold2 = static_cast<int>(detectTotalLen * 0.25f);
    m_vSyncDetectThreshold  = static_cast<int>(detectTotalLen * 0.5f);
}

void PALDecoder::initFilters()
{
    float rate = static_cast<float>(m_sampleRate);
    float videoCutoff = std::min(5.5e6f, rate * 0.4f);
    int videoTaps = std::max(17, static_cast<int>(rate / videoCutoff) * 4 + 1);
    if (videoTaps > 65) videoTaps = 65;
    if (videoTaps % 2 == 0) videoTaps++;
    m_videoFilterTaps = designLowPassFIR(videoCutoff, rate, videoTaps);
    float lumaCutoff = std::min(3.0e6f, m_decimatedRate * 0.35f);
    m_lumaFilterTaps = designLowPassFIR(lumaCutoff, m_decimatedRate, 33);
    if (COLOR_CARRIER_FREQ < rate / 2.0f)
        m_chromaFilterTaps = designBandPassFIR(COLOR_CARRIER_FREQ, m_chromaBandwidth, rate, 65);
    else
        m_chromaFilterTaps.clear();
}

void PALDecoder::rebuildColorLUT()
{
    float rate = static_cast<float>(m_sampleRate);
    if (COLOR_CARRIER_FREQ >= rate / 2.0f) {
        m_colorCarrierSin.clear();
        m_colorCarrierCos.clear();
        return;
    }
    int carrierSamples = static_cast<int>(rate / COLOR_CARRIER_FREQ * 100.0f + 0.5f);
    if (carrierSamples < 200) carrierSamples = 200;
    m_colorCarrierSin.resize(carrierSamples);
    m_colorCarrierCos.resize(carrierSamples);
    for (int i = 0; i < carrierSamples; i++) {
        double phase = 2.0 * M_PI * COLOR_CARRIER_FREQ * i / static_cast<double>(rate);
        m_colorCarrierSin[i] = static_cast<float>(std::sin(phase));
        m_colorCarrierCos[i] = static_cast<float>(std::cos(phase));
    }
}

void PALDecoder::setTuneFrequency(uint64_t freqHz)
{
    m_tuneFrequency = freqHz;
    updateNCO();
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
                          / static_cast<double>(m_sampleRate);
    m_ncoPhase = 0.0;
}

std::vector<float> PALDecoder::designLowPassFIR(float cutoff, float sampleRate, int numTaps)
{
    std::vector<float> taps(numTaps);
    float fc = cutoff / sampleRate;
    int M = numTaps - 1;
    for (int n = 0; n < numTaps; n++) {
        float mm = n - M / 2.0f;
        float h = (mm == 0.0f) ? 2.0f * fc : std::sin(2.0f * M_PI * fc * mm) / (M_PI * mm);
        float w = 0.42f - 0.5f * std::cos(2.0f * M_PI * n / M) + 0.08f * std::cos(4.0f * M_PI * n / M);
        taps[n] = h * w;
    }
    float sum = 0.0f;
    for (float t : taps) sum += t;
    if (sum != 0.0f) for (float& t : taps) t /= sum;
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
        float h = (mm == 0.0f) ? 2.0f * bw :
            (std::sin(2.0f * M_PI * (fc + bw) * mm) -
             std::sin(2.0f * M_PI * (fc - bw) * mm)) / (M_PI * mm);
        float w = 0.42f - 0.5f * std::cos(2.0f * M_PI * n / M) + 0.08f * std::cos(4.0f * M_PI * n / M);
        taps[n] = h * w * 2.0f * std::cos(2.0f * M_PI * fc * mm);
    }
    float sum = 0.0f;
    for (int i = 0; i < numTaps; i++) {
        float mm = i - (numTaps - 1) / 2.0f;
        sum += taps[i] * std::cos(2.0f * M_PI * fc * mm);
    }
    if (std::abs(sum) > 1e-6f) for (float& t : taps) t /= std::abs(sum);
    return taps;
}

std::complex<float> PALDecoder::applyVideoFilter(const std::complex<float>& sample)
{
    m_videoFilterDelay.push_front(sample);
    if (m_videoFilterDelay.size() > m_videoFilterTaps.size()) m_videoFilterDelay.pop_back();
    std::complex<float> out(0.0f, 0.0f);
    size_t n = std::min(m_videoFilterDelay.size(), m_videoFilterTaps.size());
    for (size_t i = 0; i < n; i++) out += m_videoFilterDelay[i] * m_videoFilterTaps[i];
    return out;
}

float PALDecoder::applyLumaFilter(float sample)
{
    m_lumaFilterDelay.push_front(sample);
    if (m_lumaFilterDelay.size() > m_lumaFilterTaps.size()) m_lumaFilterDelay.pop_back();
    float out = 0.0f;
    size_t n = std::min(m_lumaFilterDelay.size(), m_lumaFilterTaps.size());
    for (size_t i = 0; i < n; i++) out += m_lumaFilterDelay[i] * m_lumaFilterTaps[i];
    return out;
}

float PALDecoder::applyChromaFilterU(float sample)
{
    m_chromaUFilterDelay.push_front(sample);
    if (m_chromaUFilterDelay.size() > m_chromaFilterTaps.size()) m_chromaUFilterDelay.pop_back();
    float out = 0.0f;
    size_t n = std::min(m_chromaUFilterDelay.size(), m_chromaFilterTaps.size());
    for (size_t i = 0; i < n; i++) out += m_chromaUFilterDelay[i] * m_chromaFilterTaps[i];
    return out;
}

float PALDecoder::applyChromaFilterV(float sample)
{
    m_chromaVFilterDelay.push_front(sample);
    if (m_chromaVFilterDelay.size() > m_chromaFilterTaps.size()) m_chromaVFilterDelay.pop_back();
    float out = 0.0f;
    size_t n = std::min(m_chromaVFilterDelay.size(), m_chromaFilterTaps.size());
    for (size_t i = 0; i < n; i++) out += m_chromaVFilterDelay[i] * m_chromaFilterTaps[i];
    return out;
}

float PALDecoder::dcBlock(float sample)
{
    constexpr float alpha = 0.995f;
    float out = sample - m_dcBlockerX1 + alpha * m_dcBlockerY1;
    m_dcBlockerX1 = sample;
    m_dcBlockerY1 = out;
    return out;
}

float PALDecoder::normalizeAndAGC(float sample)
{
    if (sample < m_effMin) m_effMin = sample;
    if (sample > m_effMax) m_effMax = sample;
    m_amSampleIndex++;
    if (m_amSampleIndex >= m_samplesPerLine * NB_LINES * 2) {
        m_ampMax = m_effMax;
        float range = m_effMax - m_effMin;
        m_ampMin = m_effMin - range * 0.10f;
        m_ampDelta = m_ampMax - m_ampMin;
        if (m_ampDelta <= 0.0f) m_ampDelta = 1.0f;
        m_effMin = 20.0f;
        m_effMax = -20.0f;
        m_amSampleIndex = 0;
    }
    float normalized = (sample - m_ampMin) / m_ampDelta;
    return (normalized > 1.0f) ? 1.0f : normalized;
}

void PALDecoder::processSamples(const int8_t* data, size_t len)
{
    if (!data || len == 0) return;
    std::vector<std::complex<float>> samples;
    samples.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        samples.emplace_back(
            static_cast<float>(data[i]) / 128.0f,
            static_cast<float>(data[i + 1]) / 128.0f);
    }
    processSamples(samples);
}

void PALDecoder::processSamples(const std::vector<std::complex<float>>& samples)
{
    std::lock_guard<std::mutex> lock(m_processMutex);
    if (samples.empty() || samples.size() > 100000000) return;

    for (const auto& sample : samples) {
        m_totalSamples++;

        if (m_totalSamples % 10000000 == 0) {
            float detectionRate = (m_syncQualityWindow > 0)
                ? static_cast<float>(m_syncFoundInWindow) / m_syncQualityWindow : 0.0f;
            float avgError = (m_syncFoundInWindow > 0)
                ? static_cast<float>(m_syncErrorAccum / m_syncFoundInWindow)
                : static_cast<float>(m_numberSamplesPerHTop);
            float errorQuality = 1.0f - std::clamp(avgError / static_cast<float>(m_numberSamplesPerHTop), 0.0f, 1.0f);
            float syncQuality = (detectionRate * 0.6f + errorQuality * 0.4f) * 100.0f;
            m_lastSyncQuality = syncQuality;
            m_syncQualityWindow = 0;
            m_syncFoundInWindow = 0;
            m_syncErrorAccum = 0.0;
            if (m_syncStatsCallback)
                m_syncStatsCallback(syncQuality, m_ampMax, m_ampMin);
        }

        float ncoI = static_cast<float>(std::cos(m_ncoPhase));
        float ncoQ = static_cast<float>(std::sin(m_ncoPhase));
        m_ncoPhase += m_ncoPhaseIncrement;
        if (m_ncoPhase > M_PI) m_ncoPhase -= 2.0 * M_PI;
        else if (m_ncoPhase < -M_PI) m_ncoPhase += 2.0 * M_PI;

        std::complex<float> shifted(
            sample.real() * ncoI - sample.imag() * ncoQ,
            sample.real() * ncoQ + sample.imag() * ncoI);

        std::complex<float> filtered = applyVideoFilter(shifted);
        float magnitude = std::sqrt(filtered.real() * filtered.real() +
                                    filtered.imag() * filtered.imag());
        float dcBlocked = dcBlock(magnitude);
        float normalized = normalizeAndAGC(dcBlocked);
        processSample(normalized);

        if (m_colorMode && !m_colorCarrierSin.empty()) {
            float chromaSin = normalized * m_colorCarrierSin[m_colorCarrierIndex];
            float chromaCos = normalized * m_colorCarrierCos[m_colorCarrierIndex];
            m_colorCarrierIndex++;
            if (m_colorCarrierIndex >= static_cast<int>(m_colorCarrierSin.size()))
                m_colorCarrierIndex = 0;
            m_chromaUAccum += applyChromaFilterU(chromaSin);
            m_chromaVAccum += applyChromaFilterV(chromaCos);
        }

        m_resampleCounter++;
        if (m_resampleCounter < m_decimFactor) continue;
        m_resampleCounter = 0;

        float luma = applyLumaFilter(normalized);
        float u = 0.0f, v = 0.0f;
        if (m_colorMode && !m_colorCarrierSin.empty()) {
            float invDecim = 1.0f / static_cast<float>(m_decimFactor);
            u = m_chromaUAccum * invDecim * 2.5f;
            v = m_chromaVAccum * invDecim * 2.5f * (m_vPhaseAlternate ? -1.0f : 1.0f);
            m_chromaUAccum = 0.0f;
            m_chromaVAccum = 0.0f;
        }

        if (m_sampleOffset > m_numberSamplesPerHSync) {
            m_lineBuffer.push_back(luma);
            m_lineBufferU.push_back(u);
            m_lineBufferV.push_back(v);
        }
    }
}

void PALDecoder::processSample(float sample)
{
    if (m_hSyncEnabled) {
        if (m_prevSample >= m_syncLevel && sample < m_syncLevel
            && m_sampleOffsetDetected > m_samplesPerLine - m_numberSamplesPerHTop) {
            float frac = (sample - m_syncLevel) / (m_prevSample - sample);
            float hSyncShift = -m_sampleOffset - m_sampleOffsetFrac - frac;
            if (hSyncShift > m_samplesPerLine / 2) hSyncShift -= m_samplesPerLine;
            else if (hSyncShift < -m_samplesPerLine / 2) hSyncShift += m_samplesPerLine;
            if (std::fabs(hSyncShift) > m_numberSamplesPerHTop) {
                m_hSyncErrorCount++;
                if (m_hSyncErrorCount >= 4) { m_hSyncShift = hSyncShift; m_hSyncErrorCount = 0; }
                m_syncErrorAccum += static_cast<double>(std::fabs(hSyncShift));
            } else {
                m_hSyncShift = hSyncShift * 0.2f;
                m_hSyncErrorCount = 0;
                m_syncErrorAccum += static_cast<double>(std::fabs(hSyncShift));
            }
            m_syncDetected++;
            m_syncFoundInWindow++;
            m_sampleOffsetDetected = 0;
        } else {
            m_sampleOffsetDetected++;
        }
    }
    m_sampleOffset++;
    if (m_vSyncEnabled) {
        if (m_sampleOffset > m_fieldDetectStartPos && m_sampleOffset < m_fieldDetectEndPos)
            m_fieldDetectSampleCount += (sample < m_syncLevel) ? 1 : 0;
        if (m_sampleOffset > m_vSyncDetectStartPos && m_sampleOffset < m_vSyncDetectEndPos)
            m_vSyncDetectSampleCount += (sample < m_syncLevel) ? 1 : 0;
    }
    if (m_sampleOffset >= m_samplesPerLine) {
        float sampleOffsetFloat = m_hSyncShift + m_sampleOffsetFrac - m_samplesPerLineFrac;
        m_sampleOffset = static_cast<int>(sampleOffsetFloat);
        m_sampleOffsetFrac = sampleOffsetFloat - m_sampleOffset;
        m_hSyncShift = 0.0f;
        m_lineIndex++;
        m_linesProcessed++;
        m_syncQualityWindow++;
        processEndOfLine();
    }
    m_prevSample = sample;
}

void PALDecoder::processEndOfLine()
{
    if (m_lineIndex == VSYNC_LINES + 3 && m_fieldIndex == 0) buildFrame();
    if (m_vSyncDetectSampleCount > m_vSyncDetectThreshold &&
        (m_lineIndex < 3 || m_lineIndex > VSYNC_LINES + 1) && m_vSyncEnabled) {
        if (m_fieldDetectSampleCount > m_fieldDetectThreshold1) m_fieldIndex = 0;
        else if (m_fieldDetectSampleCount < m_fieldDetectThreshold2) m_fieldIndex = 1;
        m_lineIndex = 2;
    }
    m_fieldDetectSampleCount = 0;
    m_vSyncDetectSampleCount = 0;
    if (m_lineIndex > NB_LINES / 2 + m_fieldIndex) {
        m_lineIndex = 1;
        m_fieldIndex = 1 - m_fieldIndex;
    }
    renderLine();
    m_lineBuffer.clear();
    m_lineBufferU.clear();
    m_lineBufferV.clear();
    m_vPhaseAlternate = !m_vPhaseAlternate;
    m_colorCarrierIndex = 0;
}

void PALDecoder::renderLine()
{
    int rowIndex = m_lineIndex - FIRST_VISIBLE_LINE;
    rowIndex = rowIndex * 2 - m_fieldIndex;
    if (rowIndex < 0 || rowIndex >= VIDEO_HEIGHT) return;
    int activeSamples = static_cast<int>(m_lineBuffer.size());
    if (activeSamples < 10) return;

    std::vector<float> currentLineU(VIDEO_WIDTH, 0.0f);
    std::vector<float> currentLineV(VIDEO_WIDTH, 0.0f);

    for (int x = 0; x < VIDEO_WIDTH; x++) {
        float srcX = (x * activeSamples) / static_cast<float>(VIDEO_WIDTH);
        int idx = static_cast<int>(srcX);
        float frac = srcX - idx;
        uint8_t r, g, b;
        if (idx >= activeSamples) {
            r = g = b = 0;
        } else {
            float Y = m_lineBuffer[idx];
            if (idx + 1 < activeSamples) Y += (m_lineBuffer[idx + 1] - Y) * frac;
            Y = Y * m_videoGain + m_videoOffset;
            Y = clipValue(Y, 0.0f, 1.0f);
            float U = 0.0f, V = 0.0f;
            if (m_colorMode && idx < static_cast<int>(m_lineBufferU.size())) {
                int idx2 = std::min(idx + 1, static_cast<int>(m_lineBufferU.size()) - 1);
                U = m_lineBufferU[idx] + (m_lineBufferU[idx2] - m_lineBufferU[idx]) * frac;
                V = m_lineBufferV[idx] + (m_lineBufferV[idx2] - m_lineBufferV[idx]) * frac;
                currentLineU[x] = U;
                currentLineV[x] = V;
                if (!m_prevLineU.empty() && x < static_cast<int>(m_prevLineU.size())) {
                    U = (U + m_prevLineU[x]) * 0.5f;
                    V = (V - m_prevLineV[x]) * 0.5f;
                }
                U *= m_chromaGain;
                V *= m_chromaGain;
            }
            yuv2rgb(Y, U, V, r, g, b);
            if (m_videoInvert) { r = 255 - r; g = 255 - g; b = 255 - b; }
        }
        // RGBA for iOS (not BGRA like Qt)
        int offset = (rowIndex * VIDEO_WIDTH + x) * 4;
        if (offset >= 0 && offset + 3 < static_cast<int>(m_frameBuffer.size())) {
            m_frameBuffer[offset + 0] = r;
            m_frameBuffer[offset + 1] = g;
            m_frameBuffer[offset + 2] = b;
            m_frameBuffer[offset + 3] = 255;
        }
    }
    m_prevLineU = currentLineU;
    m_prevLineV = currentLineV;
}

void PALDecoder::buildFrame()
{
    m_frameCount++;
    if (m_frameCallback) {
        m_frameCallback(m_frameBuffer.data(), VIDEO_WIDTH, VIDEO_HEIGHT);
    }
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
