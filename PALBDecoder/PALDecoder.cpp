#include "PALDecoder.h"
#include <QDebug>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PALDecoder::PALDecoder(QObject *parent)
    : QObject(parent)
    , m_sampleRate(16000000)
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
    , m_syncPulseCounter(0)
    , m_syncPulseMinWidth(0)
    , m_syncPulseMaxWidth(0)
    , m_syncPulseEntryFrac(0.0f)
    , m_syncPulseEntryOffset(0)
    , m_syncPulseEntryOffsetFrac(0.0f)
    , m_syncPulseActive(false)
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
    , m_notchB0(1.0f), m_notchB1(0.0f), m_notchB2(0.0f), m_notchA1(0.0f), m_notchA2(0.0f)
    , m_notchX1(0.0f), m_notchX2(0.0f), m_notchY1(0.0f), m_notchY2(0.0f)
    , m_chromaNotchB0(1.0f), m_chromaNotchB1(0.0f), m_chromaNotchB2(0.0f), m_chromaNotchA1(0.0f), m_chromaNotchA2(0.0f)
    , m_chromaNotchX1(0.0f), m_chromaNotchX2(0.0f), m_chromaNotchY1(0.0f), m_chromaNotchY2(0.0f)
    , m_chromaNotch2X1(0.0f), m_chromaNotch2X2(0.0f), m_chromaNotch2Y1(0.0f), m_chromaNotch2Y2(0.0f)
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
    , m_burstStartSample(0)
    , m_burstEndSample(0)
    , m_burstCorrI(0.0f)
    , m_burstCorrQ(0.0f)
    , m_burstDCAccum(0.0f)
    , m_burstCosAccum(0.0f)
    , m_burstSinAccum(0.0f)
    , m_burstSampleCount(0)
    , m_burstAmplitude(0.0f)
    , m_burstValid(false)
    , m_chromaRefPhase(0.0f)
    , m_burstPhaseSmoothed(0.0f)
    , m_chromaCosRef(1.0f)
    , m_chromaSinRef(0.0f)
    , m_burstAmpSmoothed(0.04f)
{
    m_frameBuffer.resize(VIDEO_WIDTH * VIDEO_HEIGHT * 4, 0);
    m_lineBuffer.reserve(2048);
    m_lineBufferU.reserve(2048);
    m_lineBufferV.reserve(2048);

    // Apply default 16 MHz
    setSampleRate(16000000);
    updateNCO();
}

PALDecoder::~PALDecoder()
{
    qDebug() << "PALDecoder: Frames:" << m_frameCount
             << "Lines:" << m_linesProcessed
             << "SyncDetected:" << m_syncDetected
             << "LastQuality:" << m_lastSyncQuality << "%";
}

void PALDecoder::setSampleRate(int sampleRate)
{
    QMutexLocker locker(&m_processMutex);

    m_sampleRate = sampleRate;

    // Decimation for luma only. Chroma demod runs at FULL sample rate
    // (before decimation) so color works at all rates.
    //   12.5 MHz -> decim 1 -> 12.5 MHz
    //   16 MHz   -> decim 2 -> 8 MHz (luma only, chroma at 16 MHz)
    //   20 MHz   -> decim 2 -> 10 MHz (luma only, chroma at 20 MHz)
    if (m_sampleRate <= 12500000) {
        m_decimFactor = 1;
    } else {
        m_decimFactor = 2;
    }

    m_decimatedRate = static_cast<float>(m_sampleRate) / m_decimFactor;

    // Chroma bandwidth at FULL sample rate (chroma runs before decimation)
    if (m_sampleRate >= 16000000)
        m_chromaBandwidth = 1.5e6f;
    else
        m_chromaBandwidth = 1.2e6f;

    applyStandard();
    initFilters();
    initNotchFilter();
    initBurstPLL();
    rebuildColorLUT();

    // Reset state
    m_resampleCounter = 0;
    m_sampleOffset = 0;
    m_sampleOffsetFrac = 0.0f;
    m_hSyncShift = 0.0f;
    m_hSyncErrorCount = 0;
    m_lineIndex = 0;
    m_fieldIndex = 0;
    m_colorCarrierIndex = 0;
    m_syncPulseCounter = 0;
    m_syncPulseActive = false;

    // Reset burst PLL
    m_burstCorrI = 0.0f;
    m_burstCorrQ = 0.0f;
    m_burstDCAccum = 0.0f;
    m_burstCosAccum = 0.0f;
    m_burstSinAccum = 0.0f;
    m_burstSampleCount = 0;
    m_burstAmplitude = 0.0f;
    m_burstValid = false;
    m_chromaRefPhase = 0.0f;
    m_burstPhaseSmoothed = 0.0f;
    m_chromaCosRef = 1.0f;
    m_chromaSinRef = 0.0f;
    m_burstAmpSmoothed = 0.04f;

    // Reset AGC
    m_ampMin = -1.0f;
    m_ampMax = 1.0f;
    m_ampDelta = 2.0f;
    m_effMin = 20.0f;
    m_effMax = -20.0f;
    m_amSampleIndex = 0;

    // Reset notch filter state
    m_notchX1 = m_notchX2 = m_notchY1 = m_notchY2 = 0.0f;
    m_chromaNotchX1 = m_chromaNotchX2 = m_chromaNotchY1 = m_chromaNotchY2 = 0.0f;
    m_chromaNotch2X1 = m_chromaNotch2X2 = m_chromaNotch2Y1 = m_chromaNotch2Y2 = 0.0f;

    // Clear filter delays
    m_videoFilterDelay.clear();
    m_lumaFilterDelay.clear();
    m_chromaUFilterDelay.clear();
    m_chromaVFilterDelay.clear();

    qDebug() << "PALDecoder::setSampleRate:" << m_sampleRate / 1e6f << "MHz";
    qDebug() << "  Decimation:" << m_decimFactor << "-> video:" << m_decimatedRate / 1e6f << "MHz";
    qDebug() << "  Samples/line:" << m_samplesPerLine << "+" << m_samplesPerLineFrac << "(at" << m_sampleRate / 1e6f << "MHz)";
    qDebug() << "  HSync pulse:" << m_numberSamplesPerHTop << "Blanking:" << m_numberSamplesPerLineSignals;
    qDebug() << "  Sync pulse validation: min" << m_syncPulseMinWidth << "max" << m_syncPulseMaxWidth << "samples";
    qDebug() << "  Chroma BW:" << m_chromaBandwidth / 1e6f << "MHz";
}

void PALDecoder::applyStandard()
{
    // All timing computed at FULL sample rate (sync runs at full rate)
    float rate = static_cast<float>(m_sampleRate);
    float exactSPL = rate / (NB_LINES * FPS);
    m_samplesPerLine = static_cast<int>(exactSPL);
    m_samplesPerLineFrac = exactSPL - m_samplesPerLine;

    m_numberSamplesPerHTop        = static_cast<int>(SYNC_PULSE_FRAC * exactSPL);
    m_numberSamplesPerHSync       = static_cast<int>(HSYNC_FRAC * exactSPL);
    m_numberSamplesPerLineSignals = static_cast<int>(BLANKING_FRAC * exactSPL);
    m_numberSamplesHSyncCrop      = static_cast<int>(HSYNC_CROP_FRAC * exactSPL);

    // Sync pulse width validation limits:
    // Real H-sync pulse is 4.7 us. Accept pulses between ~2 us and ~7 us.
    // This rejects short video content dips (< 2 us) and VSync broad pulses (> 7 us).
    m_syncPulseMinWidth = static_cast<int>(2.0f / 64.0f * exactSPL);
    m_syncPulseMaxWidth = static_cast<int>(7.0f / 64.0f * exactSPL);

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

    // Video IQ LPF: applied to complex IQ after NCO shift, before AM demod.
    // Must pass chroma subcarrier at 4.43 MHz with minimal attenuation.
    // At 12.5 MHz: Nyquist=6.25 MHz, use 5 MHz (tight but necessary)
    // At 16+ MHz: Nyquist=8+ MHz, use 6 MHz (preserves chroma fully)
    float videoCutoff;
    if (m_sampleRate >= 16000000)
        videoCutoff = std::min(6.0e6f, rate * 0.40f);
    else
        videoCutoff = std::min(5.0e6f, rate * 0.40f);
    int videoTaps = std::max(17, static_cast<int>(rate / videoCutoff) * 4 + 1);
    if (videoTaps > 65) videoTaps = 65;
    if (videoTaps % 2 == 0) videoTaps++;
    m_videoFilterTaps = designLowPassFIR(videoCutoff, rate, videoTaps);

    // Luma LPF at decimated rate: 5.0 MHz for FULL PAL-B video bandwidth
    // PAL-B luminance bandwidth is 5.0 MHz (ITU-R BT.1700)
    // Previous 3.0 MHz was cutting off half the horizontal resolution!
    float lumaCutoff = std::min(5.0e6f, m_decimatedRate * 0.45f);
    int lumaTaps = 21;
    if (m_decimatedRate >= 10e6f) lumaTaps = 33;
    m_lumaFilterTaps = designLowPassFIR(lumaCutoff, m_decimatedRate, lumaTaps);

    // Chroma BPF at FULL sample rate (chroma demod runs before decimation)
    // More taps at higher sample rates for sharper filter.
    // PAL chroma bandwidth is ~1.3 MHz (U: 1.3 MHz, V: 1.3 MHz).
    if (COLOR_CARRIER_FREQ < rate / 2.0f) {
        int chromaTaps;
        if (m_sampleRate >= 20000000)
            chromaTaps = 45;      // 20 MHz: more headroom, sharper BPF
        else if (m_sampleRate >= 16000000)
            chromaTaps = 35;      // 16 MHz: good balance
        else
            chromaTaps = 31;      // 12.5 MHz: minimal
        m_chromaFilterTaps = designBandPassFIR(COLOR_CARRIER_FREQ, m_chromaBandwidth, rate, chromaTaps);
    } else {
        m_chromaFilterTaps.clear();
    }

    qDebug() << "  Filters: video LPF" << videoCutoff / 1e6f << "MHz @" << rate / 1e6f
             << "(" << m_videoFilterTaps.size() << "taps),"
             << "luma" << lumaCutoff / 1e6f << "MHz @" << m_decimatedRate / 1e6f
             << "(" << lumaTaps << "taps),"
             << "chroma BPF" << (m_chromaFilterTaps.empty() ? "OFF" : "ON")
             << m_chromaBandwidth / 1e6f << "MHz BW"
             << "(" << m_chromaFilterTaps.size() << "taps)"
             << "@" << rate / 1e6f << "MHz";
}

// Audio carrier notch filter: removes 5.5 MHz beat from AM-demodulated video
// After AM demod, the audio carrier (at video_carrier + 5.5 MHz) appears as a
// beat frequency in the baseband signal, causing horizontal banding artifacts.
// IIR biquad notch: H(z) = (1 - 2cos(w0)z^-1 + z^-2) / (1 - 2R*cos(w0)z^-1 + R^2*z^-2)
void PALDecoder::initNotchFilter()
{
    float rate = static_cast<float>(m_sampleRate);

    // The audio carrier is 5.5 MHz above the video carrier.
    // After IQ LPF + AM demod, it may appear as a residual beat.
    // If sample rate > 11 MHz, the beat is at 5.5 MHz directly.
    // If sample rate < 11 MHz, it aliases to (sampleRate - 5.5 MHz).
    float beatFreq = 5.5e6f;
    if (beatFreq >= rate / 2.0f) {
        beatFreq = rate - beatFreq;
        if (beatFreq < 0) beatFreq = -beatFreq;
    }

    // Design notch at the beat frequency
    float w0 = 2.0f * static_cast<float>(M_PI) * beatFreq / rate;
    float notchBW = 300e3f;  // 300 kHz notch bandwidth
    float bw = 2.0f * static_cast<float>(M_PI) * notchBW / rate;
    float R = 1.0f - bw / 2.0f;
    if (R < 0.8f) R = 0.8f;
    if (R > 0.999f) R = 0.999f;

    float cosw = std::cos(w0);
    m_notchB0 = 1.0f;
    m_notchB1 = -2.0f * cosw;
    m_notchB2 = 1.0f;
    m_notchA1 = -2.0f * R * cosw;
    m_notchA2 = R * R;

    // Normalize passband gain to 1
    float dcGain = (m_notchB0 + m_notchB1 + m_notchB2) / (1.0f + m_notchA1 + m_notchA2);
    if (std::fabs(dcGain) > 0.01f) {
        m_notchB0 /= dcGain;
        m_notchB1 /= dcGain;
        m_notchB2 /= dcGain;
    }

    m_notchX1 = m_notchX2 = m_notchY1 = m_notchY2 = 0.0f;

    qDebug() << "  Audio notch filter:" << beatFreq / 1e6f << "MHz, BW:" << notchBW / 1e3f << "kHz, R:" << R;

    // === Chroma subcarrier notch at 4.43 MHz ===
    // Cascaded 2-stage biquad for ~40 dB suppression of colour subcarrier from luma.
    // Wider BW (800 kHz) to catch subcarrier + sidebands.
    float chromaFreq = COLOR_CARRIER_FREQ;
    if (chromaFreq >= rate / 2.0f) {
        chromaFreq = rate - chromaFreq;
        if (chromaFreq < 0) chromaFreq = -chromaFreq;
    }
    float cw0 = 2.0f * static_cast<float>(M_PI) * chromaFreq / rate;
    float cBW = 800e3f;  // 800 kHz notch bandwidth
    float cbw = 2.0f * static_cast<float>(M_PI) * cBW / rate;
    float cR = 1.0f - cbw / 2.0f;
    if (cR < 0.8f) cR = 0.8f;
    if (cR > 0.999f) cR = 0.999f;

    float ccosw = std::cos(cw0);
    m_chromaNotchB0 = 1.0f;
    m_chromaNotchB1 = -2.0f * ccosw;
    m_chromaNotchB2 = 1.0f;
    m_chromaNotchA1 = -2.0f * cR * ccosw;
    m_chromaNotchA2 = cR * cR;

    float cdcGain = (m_chromaNotchB0 + m_chromaNotchB1 + m_chromaNotchB2) /
                    (1.0f + m_chromaNotchA1 + m_chromaNotchA2);
    if (std::fabs(cdcGain) > 0.01f) {
        m_chromaNotchB0 /= cdcGain;
        m_chromaNotchB1 /= cdcGain;
        m_chromaNotchB2 /= cdcGain;
    }

    m_chromaNotchX1 = m_chromaNotchX2 = m_chromaNotchY1 = m_chromaNotchY2 = 0.0f;
    m_chromaNotch2X1 = m_chromaNotch2X2 = m_chromaNotch2Y1 = m_chromaNotch2Y2 = 0.0f;

    qDebug() << "  Chroma notch filter (2-stage):" << chromaFreq / 1e6f << "MHz, BW:" << cBW / 1e3f << "kHz, R:" << cR;
}

void PALDecoder::rebuildColorLUT()
{
    float rate = static_cast<float>(m_sampleRate);

    // Color carrier LUT at full sample rate (chroma runs before decimation)
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

void PALDecoder::initBurstPLL()
{
    float rate = static_cast<float>(m_sampleRate);

    // PAL-B colour burst: 10 +/- 1 cycles of 4.43 MHz subcarrier
    // Located in the back porch, starting ~5.6 us after the leading edge of H-sync
    // and ending ~7.85 us (burst duration ~2.25 us)
    // We use sample positions at FULL sample rate since chroma runs at full rate.
    float exactSPL = rate / (NB_LINES * FPS);
    m_burstStartSample = static_cast<int>(5.6f / 64.0f * exactSPL);
    m_burstEndSample   = static_cast<int>(7.85f / 64.0f * exactSPL);

    qDebug() << "  Burst PLL: window" << m_burstStartSample << "-" << m_burstEndSample
             << "samples (" << (m_burstEndSample - m_burstStartSample) << "samples,"
             << (m_burstEndSample - m_burstStartSample) / (rate / COLOR_CARRIER_FREQ)
             << "cycles at" << rate / 1e6f << "MHz)";
}

void PALDecoder::accumulateBurst(float sample)
{
    if (m_colorCarrierSin.empty()) return;

    int idx = m_colorCarrierIndex - 1;
    if (idx < 0) idx += static_cast<int>(m_colorCarrierSin.size());

    float cosVal = m_colorCarrierCos[idx];
    float sinVal = m_colorCarrierSin[idx];

    m_burstCorrI += sample * cosVal;
    m_burstCorrQ += sample * sinVal;
    m_burstDCAccum += sample;
    m_burstCosAccum += cosVal;
    m_burstSinAccum += sinVal;
    m_burstSampleCount++;
}

void PALDecoder::extractBurstPhase()
{
    if (m_burstSampleCount < 5) {
        m_burstValid = false;
        return;
    }

    float N = static_cast<float>(m_burstSampleCount);

    // Remove DC bias from correlation:
    // Raw correlation: sum(sample * cos) = sum((DC + AC) * cos)
    //                                    = DC * sum(cos) + sum(AC * cos)
    // We want just sum(AC * cos), so subtract DC * sum(cos):
    float dcMean = m_burstDCAccum / N;
    float corrI = (m_burstCorrI - dcMean * m_burstCosAccum) / N;
    float corrQ = (m_burstCorrQ - dcMean * m_burstSinAccum) / N;

    float amplitude = std::sqrt(corrI * corrI + corrQ * corrQ);

    if (amplitude < 0.0005f) {
        m_burstValid = false;
        return;
    }

    m_burstValid = true;
    m_burstAmplitude = amplitude;

    // The burst signal is: A * sin(2*pi*fsc*t + burst_phase)
    // Our LUT is: sin(2*pi*fsc*n/fs), cos(2*pi*fsc*n/fs)
    // Correlation gives: I = A*cos(burst_phase - LUT_phase), Q = A*sin(burst_phase - LUT_phase)
    // So: measured_angle = atan2(Q, I) = burst_phase - LUT_initial_phase
    //
    // PAL burst nominal phase (relative to U subcarrier):
    //   burst = sin(wt + 180 + 45) on V-normal lines  = sin(wt + 225 deg)
    //   burst = sin(wt + 180 - 45) on V-inverted lines = sin(wt + 135 deg)
    // This means burst_phase is 225 or 135 deg from the U=0 reference.
    //
    // For U demod we need: cos(wt + LUT_offset) aligned to U axis (0 deg)
    // For V demod we need: sin(wt + LUT_offset) aligned to V axis (90 deg)
    // where LUT_offset = measured_angle - burst_nominal
    //
    // Actually simpler: the measured angle tells us where our LUT's 0-phase is
    // relative to the burst. The U reference is the burst phase minus the
    // nominal burst angle.

    float measuredAngle = std::atan2(corrQ, corrI);

    // PAL burst nominal angle (the angle of the burst relative to U axis):
    // Burst = U*sin(wt + 180+45) or U*sin(wt + 180-45)
    // In terms of cos/sin decomposition of burst:
    //   Line type A: burst at 225 deg = -cos(45)*cos(wt) - sin(45)*sin(wt) ... but
    // Let's use a cleaner model:
    //   The burst vector in (I,Q) space points at the burst phase angle.
    //   On PAL-normal lines (V not inverted): burst phase = 225 deg (= -135 deg)
    //   On PAL-inverted lines (V inverted):   burst phase = 135 deg
    // Our LUT starts at phase=0 at sample 0 of the line. The measured angle
    // is the LUT's accumulated phase at the burst position MINUS the burst's
    // actual phase. So:
    //   LUT_at_burst = measuredAngle + burst_actual_phase
    // And we want the U-axis reference (0 deg). The phase correction to apply
    // to our LUT for U demod = -(measuredAngle - burst_nominal)
    //
    // Even simpler approach: just use the raw measured phase directly.
    // The measured phase IS the offset between our LUT and the burst.
    // The burst's known angle tells us where U-axis is relative to the burst.
    // refPhase = measuredAngle - burst_nominal_angle

    float burstNominalRad;
    if (m_vPhaseAlternate) {
        // V-inverted line: burst at +135 deg
        burstNominalRad = 135.0f * static_cast<float>(M_PI) / 180.0f;
    } else {
        // V-normal line: burst at +225 deg = -135 deg
        burstNominalRad = -135.0f * static_cast<float>(M_PI) / 180.0f;
    }

    float refPhase = measuredAngle - burstNominalRad;

    // Normalize to [-pi, pi]
    while (refPhase > static_cast<float>(M_PI))  refPhase -= 2.0f * static_cast<float>(M_PI);
    while (refPhase < -static_cast<float>(M_PI)) refPhase += 2.0f * static_cast<float>(M_PI);

    // Smooth the phase with circular averaging
    float alpha = 0.4f;
    float dPhase = refPhase - m_burstPhaseSmoothed;
    if (dPhase > static_cast<float>(M_PI))  dPhase -= 2.0f * static_cast<float>(M_PI);
    if (dPhase < -static_cast<float>(M_PI)) dPhase += 2.0f * static_cast<float>(M_PI);
    m_burstPhaseSmoothed += alpha * dPhase;
    while (m_burstPhaseSmoothed > static_cast<float>(M_PI))  m_burstPhaseSmoothed -= 2.0f * static_cast<float>(M_PI);
    while (m_burstPhaseSmoothed < -static_cast<float>(M_PI)) m_burstPhaseSmoothed += 2.0f * static_cast<float>(M_PI);

    m_chromaRefPhase = m_burstPhaseSmoothed;

    // Cache cos/sin for this line (avoids per-sample trig calls)
    m_chromaCosRef = std::cos(m_chromaRefPhase);
    m_chromaSinRef = std::sin(m_chromaRefPhase);

    // Smooth burst amplitude for chroma AGC
    m_burstAmpSmoothed = m_burstAmpSmoothed * 0.9f + amplitude * 0.1f;

    // Reset accumulators for next line
    m_burstCorrI = 0.0f;
    m_burstCorrQ = 0.0f;
    m_burstSampleCount = 0;
}

float PALDecoder::chromaDemodU(float sample)
{
    if (m_colorCarrierSin.empty()) return 0.0f;

    int idx = m_colorCarrierIndex - 1;
    if (idx < 0) idx += static_cast<int>(m_colorCarrierSin.size());

    // U demod: multiply by cos(wt + refPhase)
    // cos(wt + phi) = cos(wt)*cos(phi) - sin(wt)*sin(phi)
    float carrier = m_colorCarrierCos[idx] * m_chromaCosRef
                  - m_colorCarrierSin[idx] * m_chromaSinRef;

    return sample * carrier;
}

float PALDecoder::chromaDemodV(float sample)
{
    if (m_colorCarrierSin.empty()) return 0.0f;

    int idx = m_colorCarrierIndex - 1;
    if (idx < 0) idx += static_cast<int>(m_colorCarrierSin.size());

    // V demod: multiply by sin(wt + refPhase)
    // sin(wt + phi) = sin(wt)*cos(phi) + cos(wt)*sin(phi)
    float carrier = m_colorCarrierSin[idx] * m_chromaCosRef
                  + m_colorCarrierCos[idx] * m_chromaSinRef;

    // PAL V-switch: V phase alternates every line
    float vSign = m_vPhaseAlternate ? -1.0f : 1.0f;

    return sample * carrier * vSign;
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
        // UHF: channel edge = 470 + n*8, video carrier = edge + 1.25
        int n = static_cast<int>(std::floor((tuneMHz - 470.0 + 0.5) / 8.0));
        if (n < 0) n = 0;
        double channelStart = 470.0 + n * 8.0;
        videoCarrierMHz = channelStart + 1.25;
    } else if (tuneMHz >= 174.0 && tuneMHz <= 230.0) {
        int n = static_cast<int>(std::floor((tuneMHz - 174.0 + 0.5) / 8.0));
        if (n < 0) n = 0;
        double channelStart = 174.0 + n * 8.0;
        videoCarrierMHz = channelStart + 1.25;
    } else {
        videoCarrierMHz = tuneMHz;
    }
    m_videoCarrierOffsetHz = static_cast<float>((videoCarrierMHz - tuneMHz) * 1.0e6);
    m_ncoPhaseIncrement = -2.0 * M_PI * static_cast<double>(m_videoCarrierOffsetHz)
                          / static_cast<double>(m_sampleRate);
    m_ncoPhase = 0.0;
    qDebug() << "PALDecoder::updateNCO: tune=" << tuneMHz << "MHz, vc=" << videoCarrierMHz
             << "MHz, offset=" << m_videoCarrierOffsetHz << "Hz";
}

// ============================================================
// Filter Design
// ============================================================

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

std::vector<float> PALDecoder::designBandPassFIR(float centerFreq, float bandwidth,
                                                  float sampleRate, int numTaps)
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

// ============================================================
// Filter Application
// ============================================================

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

    // Update AGC every half frame (~312 lines) for faster convergence.
    // Previously 2 full frames (1250 lines) - too slow for initial lock.
    if (m_amSampleIndex >= m_samplesPerLine * NB_LINES / 2) {
        m_ampMax = m_effMax;
        m_ampMin = m_effMin;
        m_ampDelta = m_ampMax - m_ampMin;
        if (m_ampDelta <= 0.001f) m_ampDelta = 1.0f;
        m_effMin = 20.0f;
        m_effMax = -20.0f;
        m_amSampleIndex = 0;
    }

    float normalized = (sample - m_ampMin) / m_ampDelta;
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    return normalized;
}

// ============================================================
// Main Processing Loop
// ============================================================

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
    QMutexLocker locker(&m_processMutex);
    if (samples.empty() || samples.size() > 100000000) return;

    for (const auto& sample : samples) {
        m_totalSamples++;

        if (m_totalSamples % 10000000 == 0) {
            // Flywheel-based sync quality:
            // Component 1: detection rate (0..1) - were sync pulses found?
            // Component 2: error quality (0..1) - how small were flywheel corrections?
            float detectionRate = (m_syncQualityWindow > 0)
                ? static_cast<float>(m_syncFoundInWindow) / m_syncQualityWindow
                : 0.0f;

            // Average error per detected line (in samples)
            float avgError = (m_syncFoundInWindow > 0)
                ? static_cast<float>(m_syncErrorAccum / m_syncFoundInWindow)
                : static_cast<float>(m_numberSamplesPerHTop);

            // Normalize: 0 error = 1.0, error >= HTop = 0.0
            float errorQuality = 1.0f - std::clamp(avgError / static_cast<float>(m_numberSamplesPerHTop), 0.0f, 1.0f);

            // Combined: 60% detection rate + 40% error quality
            float syncQuality = (detectionRate * 0.6f + errorQuality * 0.4f) * 100.0f;
            m_lastSyncQuality = syncQuality;

            // Reset window
            m_syncQualityWindow = 0;
            m_syncFoundInWindow = 0;
            m_syncErrorAccum = 0.0;

            QMetaObject::invokeMethod(this, [this, syncQuality]() {
                emit syncStatsUpdated(syncQuality, m_ampMax, m_ampMin);
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
            sample.real() * ncoQ + sample.imag() * ncoI);

        // Video IQ LPF at full rate
        std::complex<float> filtered = applyVideoFilter(shifted);

        // AM envelope
        float magnitude = std::sqrt(filtered.real() * filtered.real() +
                                    filtered.imag() * filtered.imag());

        // Audio carrier notch filter - remove 5.5 MHz beat
        float notched = m_notchB0 * magnitude + m_notchB1 * m_notchX1 + m_notchB2 * m_notchX2
                       - m_notchA1 * m_notchY1 - m_notchA2 * m_notchY2;
        m_notchX2 = m_notchX1; m_notchX1 = magnitude;
        m_notchY2 = m_notchY1; m_notchY1 = notched;

        float dcBlocked = dcBlock(notched);
        float normalized = normalizeAndAGC(dcBlocked);

        // === PAL-B NEGATIVE MODULATION INVERSION ===
        // AM demod (magnitude) produces a signal where sync tips are at the
        // HIGHEST level and white is at the LOWEST level. This is because
        // PAL-B uses negative modulation: sync = max RF power, white = min.
        // After normalizeAndAGC, sync tips are ~0.9-1.0 and white is ~0.3.
        //
        // Sync detection looks for downward zero-crossings below m_syncLevel,
        // but the signal never goes below ~0.3, so sync is never found.
        //
        // Solution: invert the signal HERE so that:
        //   sync tips -> ~0.0-0.1 (below threshold)
        //   black level -> ~0.3
        //   white -> ~0.7
        // This matches standard PAL video levels and makes sync detection work.
        // The inversion is applied to both sync and luma paths for consistency.
        // Chroma uses raw magnitude (before AGC) so it is unaffected.
        float video = m_videoInvert ? (1.0f - normalized) : normalized;

        // Sync at full rate (inverted signal: sync tips are now LOW)
        processSample(video);

        // === CHROMA at FULL sample rate (before decimation) ===
        // Demod on raw magnitude — the chroma BPF (bandpass at 4.43 MHz)
        // inherently rejects DC and low-frequency luma content.
        // Using magnitude instead of normalized because:
        // 1. BPF already handles DC rejection (no need for dcBlock)
        // 2. Raw magnitude preserves subcarrier amplitude for proper scaling
        // 3. No AGC artifacts from normalizeAndAGC distorting chroma
        if (m_colorMode && !m_colorCarrierSin.empty()) {
            float carrierCos = m_colorCarrierCos[m_colorCarrierIndex];
            float carrierSin = m_colorCarrierSin[m_colorCarrierIndex];
            m_colorCarrierIndex++;
            if (m_colorCarrierIndex >= static_cast<int>(m_colorCarrierSin.size()))
                m_colorCarrierIndex = 0;

            // Multiply magnitude by carrier, then bandpass filter extracts chroma
            float chromaU = magnitude * carrierCos;
            float chromaV = magnitude * carrierSin * (m_vPhaseAlternate ? -1.0f : 1.0f);

            m_chromaUAccum += applyChromaFilterU(chromaU);
            m_chromaVAccum += applyChromaFilterV(chromaV);
        }

        // === Chroma subcarrier notch at FULL rate (before decimation) ===
        // Cascaded 2-stage biquad notch at 4.43 MHz — removes subcarrier
        // from luma path to prevent colour stripe artifacts (dot crawl).
        // Uses inverted video signal so luma path is consistent with sync.
        float lumaSignal;
        {
            // Stage 1
            float cn1 = m_chromaNotchB0 * video + m_chromaNotchB1 * m_chromaNotchX1 + m_chromaNotchB2 * m_chromaNotchX2
                       - m_chromaNotchA1 * m_chromaNotchY1 - m_chromaNotchA2 * m_chromaNotchY2;
            m_chromaNotchX2 = m_chromaNotchX1; m_chromaNotchX1 = video;
            m_chromaNotchY2 = m_chromaNotchY1; m_chromaNotchY1 = cn1;
            // Stage 2
            float cn2 = m_chromaNotchB0 * cn1 + m_chromaNotchB1 * m_chromaNotch2X1 + m_chromaNotchB2 * m_chromaNotch2X2
                       - m_chromaNotchA1 * m_chromaNotch2Y1 - m_chromaNotchA2 * m_chromaNotch2Y2;
            m_chromaNotch2X2 = m_chromaNotch2X1; m_chromaNotch2X1 = cn1;
            m_chromaNotch2Y2 = m_chromaNotch2Y1; m_chromaNotch2Y1 = cn2;
            lumaSignal = cn2;
        }

        // === Decimate for luma + output chroma ===
        m_resampleCounter++;
        if (m_resampleCounter < m_decimFactor) continue;
        m_resampleCounter = 0;

        // Luma at decimated rate (chroma subcarrier removed)
        float luma = applyLumaFilter(lumaSignal);

        // Chroma: average accumulated values over decimation period
        float u = 0.0f, v = 0.0f;
        if (m_colorMode && !m_colorCarrierSin.empty()) {
            float invDecim = 1.0f / static_cast<float>(m_decimFactor);
            // Scale chroma: normalize by AGC range (since we use raw magnitude),
            // then apply a gain factor. The chroma BPF output is small relative
            // to the full magnitude range, so we need significant gain.
            float chromaScale = (m_ampDelta > 0.001f) ? (invDecim * 8.0f / m_ampDelta) : invDecim;
            u = m_chromaUAccum * chromaScale;
            v = m_chromaVAccum * chromaScale;
            m_chromaUAccum = 0.0f;
            m_chromaVAccum = 0.0f;
        }

        // Collect pixels after blanking
        if (m_sampleOffset > m_numberSamplesPerHSync) {
            m_lineBuffer.push_back(luma);
            m_lineBufferU.push_back(u);
            m_lineBufferV.push_back(v);
        }
    }
}

// ============================================================
// Sync Detection (SDRangel-style)
// ============================================================

void PALDecoder::processSample(float sample)
{
    if (m_hSyncEnabled)
    {
        // === Sync Pulse Width Validation ===
        // Track how long the signal stays below sync threshold.
        // Real H-sync pulse is ~4.7 us. Video content dips are < 1 us.
        // Accept pulses between m_syncPulseMinWidth (~2 us) and
        // m_syncPulseMaxWidth (~7 us).

        if (sample < m_syncLevel) {
            if (!m_syncPulseActive) {
                // Leading edge: signal just dropped below threshold
                m_syncPulseActive = true;
                m_syncPulseCounter = 1;
                // Record the leading edge position (for timing reference)
                // Use fractional zero-crossing interpolation
                float denom = m_prevSample - sample;
                m_syncPulseEntryFrac = (denom > 1e-6f)
                    ? (m_prevSample - m_syncLevel) / denom
                    : 0.0f;
                // Save current sampleOffset at the leading edge
                m_syncPulseEntryOffset = m_sampleOffset;
                m_syncPulseEntryOffsetFrac = m_sampleOffsetFrac;
            } else {
                m_syncPulseCounter++;
                // Too wide -> not H-sync (probably V-sync equalizing pulse)
                if (m_syncPulseCounter > m_syncPulseMaxWidth) {
                    m_syncPulseActive = false;
                    m_syncPulseCounter = 0;
                }
            }
        } else {
            if (m_syncPulseActive) {
                // Trailing edge: pulse ended. Validate width.
                if (m_syncPulseCounter >= m_syncPulseMinWidth
                    && m_syncPulseCounter <= m_syncPulseMaxWidth
                    && m_sampleOffsetDetected > m_samplesPerLine - m_numberSamplesPerHTop)
                {
                    // Valid H-sync pulse!
                    // Compute flywheel correction from the LEADING edge position.
                    // The leading edge should ideally be at sampleOffset=0.
                    // hSyncShift = how far off the flywheel was at that moment.
                    float hSyncShift = -(static_cast<float>(m_syncPulseEntryOffset)
                                       + m_syncPulseEntryOffsetFrac
                                       + m_syncPulseEntryFrac);

                    if (hSyncShift > m_samplesPerLine / 2)
                        hSyncShift -= m_samplesPerLine;
                    else if (hSyncShift < -m_samplesPerLine / 2)
                        hSyncShift += m_samplesPerLine;

                    if (std::fabs(hSyncShift) > m_numberSamplesPerHTop) {
                        // Large error: flywheel far off or ambiguous detection
                        m_hSyncErrorCount++;
                        m_syncErrorAccum += static_cast<double>(std::fabs(hSyncShift));
                    } else {
                        // Good sync: apply correction.
                        // Use stronger correction (0.5) for faster lock.
                        // The flywheel will converge in 2-3 lines instead of 5+.
                        m_hSyncShift = hSyncShift * 0.5f;
                        m_hSyncErrorCount = 0;
                        m_syncErrorAccum += static_cast<double>(std::fabs(hSyncShift));
                    }

                    m_syncDetected++;
                    m_syncFoundInWindow++;
                    m_sampleOffsetDetected = 0;
                }
                m_syncPulseActive = false;
                m_syncPulseCounter = 0;
            }
        }
        // Always increment sampleOffsetDetected (even during pulse)
        // This tracks distance since last accepted sync for gating
        m_sampleOffsetDetected++;
    }

    m_sampleOffset++;

    if (m_vSyncEnabled)
    {
        if (m_sampleOffset > m_fieldDetectStartPos && m_sampleOffset < m_fieldDetectEndPos)
            m_fieldDetectSampleCount += (sample < m_syncLevel) ? 1 : 0;
        if (m_sampleOffset > m_vSyncDetectStartPos && m_sampleOffset < m_vSyncDetectEndPos)
            m_vSyncDetectSampleCount += (sample < m_syncLevel) ? 1 : 0;
    }

    if (m_sampleOffset >= m_samplesPerLine)
    {
        float sampleOffsetFloat = m_hSyncShift + m_sampleOffsetFrac - m_samplesPerLineFrac;
        m_sampleOffset = static_cast<int>(sampleOffsetFloat);
        m_sampleOffsetFrac = sampleOffsetFloat - m_sampleOffset;
        m_hSyncShift = 0.0f;

        m_lineIndex++;
        m_linesProcessed++;
        m_syncQualityWindow++;
        processEndOfLine();

        // Reset pulse tracking at line boundary
        m_syncPulseActive = false;
        m_syncPulseCounter = 0;
    }

    m_prevSample = sample;
}

void PALDecoder::processEndOfLine()
{
    if (m_lineIndex == VSYNC_LINES + 3 && m_fieldIndex == 0)
        buildFrame();

    if (m_vSyncDetectSampleCount > m_vSyncDetectThreshold &&
        (m_lineIndex < 3 || m_lineIndex > VSYNC_LINES + 1) && m_vSyncEnabled)
    {
        if (m_fieldDetectSampleCount > m_fieldDetectThreshold1)
            m_fieldIndex = 0;
        else if (m_fieldDetectSampleCount < m_fieldDetectThreshold2)
            m_fieldIndex = 1;
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
    // NOTE: Do NOT reset m_colorCarrierIndex here!
    // PAL subcarrier = 283.75 * fline, so there are 283.75 cycles per line.
    // Resetting to 0 each line causes a 0.75-cycle phase jump, making burst
    // phase measurement random. Let the LUT free-run; the burst PLL measures
    // and corrects the phase offset each line.

    // Reset burst PLL per-line state
    m_burstCorrI = 0.0f;
    m_burstCorrQ = 0.0f;
    m_burstDCAccum = 0.0f;
    m_burstCosAccum = 0.0f;
    m_burstSinAccum = 0.0f;
    m_burstSampleCount = 0;
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
            if (idx + 1 < activeSamples)
                Y += (m_lineBuffer[idx + 1] - Y) * frac;
            Y = Y * m_videoGain + m_videoOffset;
            Y = clipValue(Y, 0.0f, 1.0f);

            float U = 0.0f, V = 0.0f;
            if (m_colorMode && idx < static_cast<int>(m_lineBufferU.size())) {
                int idx2 = std::min(idx + 1, static_cast<int>(m_lineBufferU.size()) - 1);
                U = m_lineBufferU[idx] + (m_lineBufferU[idx2] - m_lineBufferU[idx]) * frac;
                V = m_lineBufferV[idx] + (m_lineBufferV[idx2] - m_lineBufferV[idx]) * frac;
                currentLineU[x] = U;
                currentLineV[x] = V;
                // PAL line averaging for phase error cancellation:
                // Since PAL switching is applied during demod (V already flipped),
                // average both U and V with previous line using addition
                if (!m_prevLineU.empty() && x < static_cast<int>(m_prevLineU.size())) {
                    U = (U + m_prevLineU[x]) * 0.5f;
                    V = (V + m_prevLineV[x]) * 0.5f;
                }
                U *= m_chromaGain;
                V *= m_chromaGain;
            }

            yuv2rgb(Y, U, V, r, g, b);
            // Note: m_videoInvert is now applied early in processSamples()
            // (before sync detection and luma filtering), so the signal
            // arriving here is already in correct polarity. No RGB inversion needed.
        }

        int offset = (rowIndex * VIDEO_WIDTH + x) * 4;
        if (offset >= 0 && offset + 3 < static_cast<int>(m_frameBuffer.size())) {
            m_frameBuffer[offset + 0] = b;
            m_frameBuffer[offset + 1] = g;
            m_frameBuffer[offset + 2] = r;
            m_frameBuffer[offset + 3] = 255;
        }
    }
    m_prevLineU = currentLineU;
    m_prevLineV = currentLineV;
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
