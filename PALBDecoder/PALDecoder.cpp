#include "PALDecoder.h"
#include <QDebug>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PALDecoder::PALDecoder(QObject *parent)
    : QObject(parent)
    , m_dcBlockerX1(0.0f)
    , m_dcBlockerY1(0.0f)
    , m_resampleCounter(0)
    , m_currentLine(0)
    , m_videoGain(2.0f)
    , m_videoOffset(0.0f)
{
    m_resampleDecim = SAMP_RATE / VIDEO_SAMP_RATE; // 16M / 6M = 2.666... ≈ 3
    
    // Initialize frame buffer
    m_frameBuffer.resize(VIDEO_WIDTH * VIDEO_HEIGHT, 128);
    
    // Initialize filters
    initFilters();
    
    qDebug() << "PAL Decoder initialized:";
    qDebug() << "  Sample rate:" << SAMP_RATE << "Hz";
    qDebug() << "  Video sample rate:" << VIDEO_SAMP_RATE << "Hz";
    qDebug() << "  Samples per line:" << SAMPLES_PER_LINE;
    qDebug() << "  Lines per frame:" << LINES_PER_FRAME;
    qDebug() << "  Visible lines:" << VISIBLE_LINES;
    qDebug() << "  Frame size:" << VIDEO_WIDTH << "x" << VIDEO_HEIGHT;
}

PALDecoder::~PALDecoder()
{
}

void PALDecoder::initFilters()
{
    // Design video low-pass filter (5 MHz cutoff at 16 MHz sample rate)
    m_videoFilterTaps = designLowPassFIR(5.0e6f, SAMP_RATE, 65);
    
    // Design luminance filter (3 MHz cutoff at 6 MHz sample rate)
    m_lumaFilterTaps = designLowPassFIR(3.0e6f, VIDEO_SAMP_RATE, 65);
    
    qDebug() << "Video filter taps:" << m_videoFilterTaps.size();
    qDebug() << "Luma filter taps:" << m_lumaFilterTaps.size();
}

std::vector<float> PALDecoder::designLowPassFIR(float cutoff, float sampleRate, int numTaps)
{
    std::vector<float> taps(numTaps);
    float fc = cutoff / sampleRate;
    int M = numTaps - 1;
    
    // Hamming window FIR filter design
    for (int n = 0; n < numTaps; n++) {
        float mm = n - M / 2.0f;
        
        // Ideal low-pass filter (sinc function)
        float h;
        if (mm == 0.0f) {
            h = 2.0f * fc;
        } else {
            h = std::sin(2.0f * M_PI * fc * mm) / (M_PI * mm);
        }
        
        // Hamming window
        float w = 0.54f - 0.46f * std::cos(2.0f * M_PI * n / M);
        
        taps[n] = h * w;
    }
    
    // Normalize
    float sum = 0.0f;
    for (float tap : taps) {
        sum += tap;
    }
    if (sum != 0.0f) {
        for (float& tap : taps) {
            tap /= sum;
        }
    }
    
    return taps;
}

std::complex<float> PALDecoder::applyVideoFilter(const std::complex<float>& sample)
{
    // Add new sample to delay line
    m_videoFilterDelay.push_front(sample);
    
    // Keep only necessary samples
    if (m_videoFilterDelay.size() > m_videoFilterTaps.size()) {
        m_videoFilterDelay.pop_back();
    }
    
    // Compute filtered output
    std::complex<float> output(0.0f, 0.0f);
    size_t delaySize = std::min(m_videoFilterDelay.size(), m_videoFilterTaps.size());
    
    for (size_t i = 0; i < delaySize; i++) {
        output += m_videoFilterDelay[i] * m_videoFilterTaps[i];
    }
    
    return output;
}

float PALDecoder::applyLumaFilter(float sample)
{
    // Add new sample to delay line
    m_lumaFilterDelay.push_front(sample);
    
    // Keep only necessary samples
    if (m_lumaFilterDelay.size() > m_lumaFilterTaps.size()) {
        m_lumaFilterDelay.pop_back();
    }
    
    // Compute filtered output
    float output = 0.0f;
    size_t delaySize = std::min(m_lumaFilterDelay.size(), m_lumaFilterTaps.size());
    
    for (size_t i = 0; i < delaySize; i++) {
        output += m_lumaFilterDelay[i] * m_lumaFilterTaps[i];
    }
    
    return output;
}

float PALDecoder::dcBlock(float sample)
{
    // DC blocker: y[n] = x[n] - x[n-1] + 0.999 * y[n-1]
    constexpr float alpha = 0.999f;
    float output = sample - m_dcBlockerX1 + alpha * m_dcBlockerY1;
    m_dcBlockerX1 = sample;
    m_dcBlockerY1 = output;
    return output;
}

float PALDecoder::clipValue(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

void PALDecoder::processSamples(const int8_t* data, size_t len)
{
    if (!data || len == 0) return;
    
    // Convert int8_t IQ pairs to complex<float>
    // HackRF uses interleaved I,Q format with int8 values
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
        // Step 1: Low-pass filter (5 MHz video bandwidth)
        std::complex<float> filtered = applyVideoFilter(sample);
        
        // Step 2: AM Demodulation (envelope detection - complex to magnitude)
        float magnitude = std::sqrt(filtered.real() * filtered.real() + 
                                   filtered.imag() * filtered.imag());
        
        // Step 3: DC blocking
        float dcBlocked = dcBlock(magnitude);
        
        // Step 4: Resampling from 16 MHz to 6 MHz (decimate by ~2.67)
        // Simple decimation: keep every Nth sample
        m_resampleCounter++;
        if (m_resampleCounter >= m_resampleDecim) {
            m_resampleCounter = 0;
            
            // Step 5: Luminance filter (3 MHz, removes 4.43 MHz color subcarrier)
            float luma = applyLumaFilter(dcBlocked);
            
            // Step 6: Apply gain and offset
            float adjusted = luma * m_videoGain + m_videoOffset;
            
            // Step 7: Clip to 0-1 range
            float clipped = clipValue(adjusted, 0.0f, 1.0f);
            
            // Step 8: Process video sample (build frame)
            processVideoSample(clipped);
        }
    }
}

void PALDecoder::processVideoSample(float sample)
{
    m_videoBuffer.push_back(sample);
    
    // Check if we have a complete line
    if (m_videoBuffer.size() >= SAMPLES_PER_LINE) {
        m_currentLine++;
        
        // Check if this is a visible line (skip vertical blanking interval)
        if (m_currentLine >= FIRST_VISIBLE_LINE && 
            m_currentLine < FIRST_VISIBLE_LINE + VISIBLE_LINES) {
            
            int lineIndex = m_currentLine - FIRST_VISIBLE_LINE;
            
            // Copy line to frame buffer
            for (int x = 0; x < SAMPLES_PER_LINE && x < VIDEO_WIDTH; x++) {
                if (x < static_cast<int>(m_videoBuffer.size())) {
                    uint8_t pixelValue = static_cast<uint8_t>(m_videoBuffer[x] * 255.0f);
                    m_frameBuffer[lineIndex * VIDEO_WIDTH + x] = pixelValue;
                }
            }
        }
        
        // Clear buffer for next line
        m_videoBuffer.clear();
        
        // Check if we have a complete frame
        if (m_currentLine >= LINES_PER_FRAME) {
            buildFrame();
            m_currentLine = 0;
        }
    }
}

void PALDecoder::buildFrame()
{
    // Create QImage from frame buffer
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
