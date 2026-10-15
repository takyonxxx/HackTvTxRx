#include "PALDecoder.h"
#include "core/SimdDot.h"
#include <QDebug>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// Colour decoding trim constants
// ============================================================
// PAL_HUE_OFFSET_DEG: if all hues look uniformly ROTATED (e.g. skin tones
//   greenish/purplish but stable), adjust this in small steps (+/-15..90).
// PAL_VSWITCH_POLARITY: if colours look DESATURATED with only blue/yellow
//   tint remaining (V cancels in line averaging), set this to 1.
// PAL_V_AXIS_SIGN: if hues look MIRRORED (red <-> cyan-green swapped),
//   set this to -1.0f.
#define PAL_HUE_OFFSET_DEG   0.0f
#define PAL_VSWITCH_POLARITY 0
#define PAL_V_AXIS_SIGN      1.0f

static inline float wrapPi(float a)
{
    while (a >  static_cast<float>(M_PI)) a -= 2.0f * static_cast<float>(M_PI);
    while (a < -static_cast<float>(M_PI)) a += 2.0f * static_cast<float>(M_PI);
    return a;
}

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
    , m_numberSamplesActiveStart(0)
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
    , m_chromaLPUState(0.0f)
    , m_chromaLPVState(0.0f)
    , m_chromaLPCoeff(0.3f)
    , m_dcBlockerX1(0.0f)
    , m_dcBlockerY1(0.0f)
    , m_dcBlockAlpha(0.99998f)
    , m_resampleCounter(0)
    , m_syncLPState(0.5f)
    , m_syncLPCoeff(0.4f)
    , m_syncLockCount(0)
    , m_syncLocked(false)
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
    , m_scPhase(0.0)
    , m_scPhaseInc(0.0)
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
    , m_burstMeanInit(false)
    , m_burstMeanPhase(0.0f)
    , m_prevBurstAngle(0.0f)
    , m_prevBurstValid(false)
    , m_burstSeenThisLine(false)
    , m_burstMissCount(0)
    , m_chromaMute(true)
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

    // DC blocker: ~30 Hz cutoff. The previous fixed alpha=0.995 gave a
    // ~10 kHz cutoff (time constant shorter than one 64 us line!), which
    // tilted every line and made the sync tip level content-dependent.
    float rateF = static_cast<float>(m_sampleRate);
    m_dcBlockAlpha = 1.0f - (2.0f * static_cast<float>(M_PI) * 30.0f) / rateF;
    if (m_dcBlockAlpha < 0.99f) m_dcBlockAlpha = 0.99f;

    // Sync path one-pole LPF: ~1 MHz cutoff smooths 8-bit ADC noise on the
    // threshold comparator without meaningfully delaying the sync edge.
    m_syncLPCoeff = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * 1.0e6f / rateF);
    m_syncLPState = 0.5f;

    // Reset state
    m_resampleCounter = 0;
    m_sampleOffset = 0;
    m_sampleOffsetFrac = 0.0f;
    m_hSyncShift = 0.0f;
    m_hSyncErrorCount = 0;
    m_lineIndex = 0;
    m_fieldIndex = 0;
    m_vSyncValid = false;
    m_vSyncRejects = 0;
    m_linesSinceVSync = 0;
    m_scPhase = 0.0;
    m_syncPulseCounter = 0;
    m_syncPulseActive = false;
    m_syncLockCount = 0;
    m_syncLocked = false;

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
    m_chromaCosRef = 1.0f;
    m_chromaSinRef = 0.0f;
    m_burstAmpSmoothed = 0.04f;
    m_burstMeanInit = false;
    m_burstMeanPhase = 0.0f;
    m_prevBurstAngle = 0.0f;
    m_prevBurstValid = false;
    m_burstSeenThisLine = false;
    m_burstMissCount = 0;
    m_chromaMute = true;

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
    resetHist(m_videoHistRe, m_videoFilterTaps);
    resetHist(m_videoHistIm, m_videoFilterTaps);
    resetHist(m_lumaHist,    m_lumaFilterTaps);
    resetHist(m_chromaHist,  m_chromaFilterTaps);
    m_prevLineU.clear();
    m_prevLineV.clear();
    m_chromaLPUState = 0.0f;
    m_chromaLPVState = 0.0f;
    // Post-demod chroma LPF: ~800 kHz (removes the 2*fsc product term)
    m_chromaLPCoeff = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * 8.0e5f / rateF);

    // Carrier tracker (200 kHz) and VSB-compensation shelf (~450 kHz corner)
    m_carrLPCoeff = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * 2.0e5f / rateF);
    m_carrLP = m_carrPrev = std::complex<float>(0.0f, 0.0f);
    m_vsbLPCoeff = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * 4.5e5f / rateF);
    m_vsbLPState = 0.0f;
    m_afcWindow = std::max(1, m_sampleRate / 20);   // 50 ms
    updateNCO();   // NCO step depends on the sample rate (also resets AFC)

}

void PALDecoder::applyStandard()
{
    // All timing computed at FULL sample rate (sync runs at full rate)
    float rate = static_cast<float>(m_sampleRate);
    float exactSPL = rate / (NB_LINES * FPS);
    m_samplesPerLine = static_cast<int>(exactSPL);
    m_samplesPerLineFrac = exactSPL - m_samplesPerLine;

    m_numberSamplesPerHTop        = static_cast<int>(SYNC_PULSE_FRAC * exactSPL);
    m_numberSamplesActiveStart       = static_cast<int>(ACTIVE_VIDEO_START_FRAC * exactSPL);
    m_numberSamplesPerLineSignals = static_cast<int>(BLANKING_FRAC * exactSPL);
    m_numberSamplesHSyncCrop      = static_cast<int>(HSYNC_CROP_FRAC * exactSPL);

    // Sync pulse width validation limits:
    // Real H-sync pulse is 4.7 us. Accept pulses between 3.0 us and 6.5 us.
    // This rejects video content dips, VSync equalizing pulses (2.35 us)
    // and VSync broad pulses (~27 us). With the sync-path LPF smoothing the
    // comparator input, the measured width stays close to the true 4.7 us.
    m_syncPulseMinWidth = static_cast<int>(3.0f / 64.0f * exactSPL);
    m_syncPulseMaxWidth = static_cast<int>(6.5f / 64.0f * exactSPL);

    // Auto-slicer windows: sync tip (inside the 4.7 us pulse, away from edges)
    // and back porch after the burst (black level).
    m_tipStart    = static_cast<int>(1.5f / 64.0f * exactSPL);
    m_tipEnd      = static_cast<int>(4.0f / 64.0f * exactSPL);
    m_porchStart  = static_cast<int>(8.5f / 64.0f * exactSPL);
    m_porchEnd    = static_cast<int>(10.5f / 64.0f * exactSPL);
    m_tipSum = m_porchSum = 0.0;
    m_tipCount = m_porchCount = 0;

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

}

void PALDecoder::rebuildColorLUT()
{
    float rate = static_cast<float>(m_sampleRate);

    if (COLOR_CARRIER_FREQ >= rate / 2.0f) {
        m_colorCarrierSin.clear();
        m_colorCarrierCos.clear();
        m_scPhaseInc = 0.0;
        return;
    }

    // One full subcarrier cycle; the NCO phase accumulator indexes into it.
    m_colorCarrierSin.resize(SC_LUT_SIZE);
    m_colorCarrierCos.resize(SC_LUT_SIZE);
    for (int i = 0; i < SC_LUT_SIZE; i++) {
        double phase = 2.0 * M_PI * i / SC_LUT_SIZE;
        m_colorCarrierSin[i] = static_cast<float>(std::sin(phase));
        m_colorCarrierCos[i] = static_cast<float>(std::cos(phase));
    }
    m_scPhaseInc = 2.0 * M_PI * COLOR_CARRIER_FREQ / static_cast<double>(rate);
    m_scPhase = 0.0;
}

void PALDecoder::initBurstPLL()
{
    float rate = static_cast<float>(m_sampleRate);

    // PAL-B colour burst: 10 +/- 1 cycles of 4.43 MHz subcarrier
    // Located in the back porch, starting ~5.6 us after the leading edge of H-sync
    // and ending ~7.85 us (burst duration ~2.25 us)
    // We use sample positions at FULL sample rate since chroma runs at full rate.
    float exactSPL = rate / (NB_LINES * FPS);
    // The burst is correlated on the SAME band-filtered signal used for
    // chroma demodulation, so the linear-phase BPF group delay
    // ((taps-1)/2 samples) shifts the burst window by the same amount and
    // the phase reference stays exactly consistent with the demod path.
    int groupDelay = m_chromaFilterTaps.empty()
        ? 0 : (static_cast<int>(m_chromaFilterTaps.size()) - 1) / 2;
    m_burstStartSample = static_cast<int>(5.6f / 64.0f * exactSPL) + groupDelay;
    m_burstEndSample   = static_cast<int>(7.85f / 64.0f * exactSPL) + groupDelay;
}

void PALDecoder::accumulateBurst(float sample, float cosVal, float sinVal)
{
    m_burstCorrI += sample * cosVal;
    m_burstCorrQ += sample * sinVal;
    m_burstDCAccum += sample;
    m_burstCosAccum += cosVal;
    m_burstSinAccum += sinVal;
    m_burstSampleCount++;
}

void PALDecoder::extractBurstPhase()
{
    int count = m_burstSampleCount;
    float N = static_cast<float>(count);

    // Remove DC bias from correlation:
    //   sum(sample * cos) = DC * sum(cos) + sum(AC * cos)
    float dcMean = (count > 0) ? (m_burstDCAccum / N) : 0.0f;
    float corrI = (count > 0) ? ((m_burstCorrI - dcMean * m_burstCosAccum) / N) : 0.0f;
    float corrQ = (count > 0) ? ((m_burstCorrQ - dcMean * m_burstSinAccum) / N) : 0.0f;

    // Consume accumulators so this runs once per line
    m_burstCorrI = 0.0f;
    m_burstCorrQ = 0.0f;
    m_burstDCAccum = 0.0f;
    m_burstCosAccum = 0.0f;
    m_burstSinAccum = 0.0f;
    m_burstSampleCount = 0;
    m_burstSeenThisLine = true;

    float amplitude = std::sqrt(corrI * corrI + corrQ * corrQ);

    // Relative validity threshold: burst must be a meaningful fraction of
    // the video amplitude range (rejects noise on B/W or vsync lines).
    float minAmp = 0.008f * m_ampDelta;
    if (minAmp < 0.0005f) minAmp = 0.0005f;

    if (count < 8 || amplitude < minAmp) {
#ifdef PAL_TEST_DEBUG
        { static int di=0; if(++di%1499==0) std::printf("[burstINV] cnt=%d amp=%.5f minAmp=%.5f\n", count, amplitude, minAmp); }
#endif
        m_burstValid = false;
        m_prevBurstValid = false;
        if (m_burstMissCount < 1000) m_burstMissCount++;
        if (m_burstMissCount > 8) m_chromaMute = true;
        return;
    }

    m_burstValid = true;
    m_burstAmplitude = amplitude;
    m_burstMissCount = 0;
#ifdef PAL_TEST_DEBUG
    { static long dn=0; ++dn;
      if (dn>=25000 && dn<25012)
        std::printf("[seq] n=%ld line=%d ang=%.1f mean=%.1f prevValid=%d\n",
            dn, m_lineIndex, std::atan2(corrQ,corrI)*57.3f, m_burstMeanPhase*57.3f, (int)m_prevBurstValid); }
#endif

    // Smooth burst amplitude for chroma AGC
    m_burstAmpSmoothed = m_burstAmpSmoothed * 0.9f + amplitude * 0.1f;

    // ---- Swinging-burst mean-axis tracking ----
    // Any narrowband component can be written s = X*cos(phi_lut) + Y*sin(phi_lut);
    // the correlation returns exactly (X, Y), so the measured angle is the
    // burst vector's angle in the LUT (cos, sin) plane.
    //
    // PAL burst sits at (U-axis + 180 +/- 45 deg), alternating each line.
    // Vector-averaging two CONSECUTIVE line bursts cancels the +/-45 swing
    // and yields the mean burst axis (= U axis + 180 deg) directly, without
    // needing to know the line type in advance.
    float measuredAngle = std::atan2(corrQ, corrI);

    if (m_prevBurstValid) {
        float mx = std::cos(measuredAngle) + std::cos(m_prevBurstAngle);
        float my = std::sin(measuredAngle) + std::sin(m_prevBurstAngle);
        // If the two vectors are near-opposite the mean is undefined; skip.
        if (mx * mx + my * my > 0.1f) {
            float meanTarget = std::atan2(my, mx);
            if (!m_burstMeanInit) {
                m_burstMeanPhase = meanTarget;
                m_burstMeanInit = true;
            } else {
                float d = wrapPi(meanTarget - m_burstMeanPhase);
                m_burstMeanPhase = wrapPi(m_burstMeanPhase + 0.25f * d);
            }
        }
    }
    m_prevBurstAngle = measuredAngle;
    m_prevBurstValid = true;

    if (!m_burstMeanInit) return;

    // ---- Per-line V-switch detection from burst residual ----
    // Residual of this line's burst around the mean axis is +/-45 deg;
    // its sign identifies the PAL switch state of THIS line. This replaces
    // the blind every-line toggle (which loses phase across dropouts/vsync).
    float resid = wrapPi(measuredAngle - m_burstMeanPhase);
    bool vInverted = (resid > 0.0f);
#if PAL_VSWITCH_POLARITY
    vInverted = !vInverted;
#endif
    m_vPhaseAlternate = vInverted;

    // ---- U-axis reference ----
    // U axis = mean burst axis - 180 deg (+ optional hue trim).
    float alphaU = wrapPi(m_burstMeanPhase - static_cast<float>(M_PI)
                          + PAL_HUE_OFFSET_DEG * static_cast<float>(M_PI) / 180.0f);
    m_chromaRefPhase = alphaU;
    m_chromaCosRef = std::cos(alphaU);
    m_chromaSinRef = std::sin(alphaU);

    m_chromaMute = false;
}

void PALDecoder::setTuneFrequency(uint64_t freqHz)
{
    QMutexLocker locker(&m_processMutex);   // AFC state is touched by the DSP thread
    m_tuneFrequency = freqHz;
    updateNCO();
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
    m_afcTrimHz = 0.0f;   // new tuning: restart AFC from the nominal carrier
    m_ncoPhase = 0.0;
    applyNcoIncrement();
    m_ncoOsc = std::complex<double>(1.0, 0.0);
    m_ncoRenormCount = 0;
    m_afcAcc = {0.0, 0.0};
    m_afcSigPow = m_afcTotPow = 0.0;
    m_afcCount = 0;
}

void PALDecoder::applyNcoIncrement()
{
    m_ncoPhaseIncrement = -2.0 * M_PI
        * (static_cast<double>(m_videoCarrierOffsetHz) + static_cast<double>(m_afcTrimHz))
        / static_cast<double>(m_sampleRate);
    m_ncoStep = std::polar(1.0, m_ncoPhaseIncrement);
}

// Frequency error of the residual carrier (after the NCO shift), measured
// from the phase advance of the narrowband carrier estimate. Only applied
// when the carrier clearly dominates its 200 kHz neighbourhood.
void PALDecoder::runAfc()
{
    const double carrierFraction = (m_afcTotPow > 1e-20) ? (m_afcSigPow / m_afcTotPow) : 0.0;
    if (carrierFraction > 0.15 && std::abs(m_afcAcc) > 0.0) {
        double errHz = std::arg(m_afcAcc) * static_cast<double>(m_sampleRate) / (2.0 * M_PI);
        float trim = m_afcTrimHz + 0.3f * static_cast<float>(errHz);
        m_afcTrimHz = std::clamp(trim, -150000.0f, 150000.0f);
        applyNcoIncrement();
    }
    m_afcAcc = {0.0, 0.0};
    m_afcSigPow = m_afcTotPow = 0.0;
    m_afcCount = 0;
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
        // LOW-PASS prototype (cutoff = bw/2), then modulate to fc.
        // NOTE: the previous version used a band-pass difference-of-sincs
        // kernel here AND modulated it by cos again, which double-shifted
        // the band to DC and 2*fc. Combined with the passband-at-fc
        // normalization this gave the "band-pass" a DC gain of ~14, so the
        // envelope's DC leaked into the chroma path 14x stronger than the
        // actual colour subcarrier.
        float h = (mm == 0.0f) ? 2.0f * bw
                               : std::sin(2.0f * M_PI * bw * mm) / (M_PI * mm);
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

void PALDecoder::resetHist(std::vector<float>& hist, const std::vector<float>& taps)
{
    hist.assign(taps.empty() ? 0 : taps.size() - 1, 0.0f);
}

// out[i] = sum_k taps[k] * x[i-k]  (causal FIR, same tap order / group delay as
// the former per-sample circular buffer). One tap per pass over the whole
// block: no reductions, so each pass is a plain 4-wide multiply-add.
void PALDecoder::blockFir(const float* in, float* out, int n, const std::vector<float>& taps,
                          std::vector<float>& hist, std::vector<float>& work)
{
    const int ntaps = static_cast<int>(taps.size());
    if (ntaps == 0 || n <= 0) {
        if (n > 0) std::memcpy(out, in, n * sizeof(float));
        return;
    }
    const int h = ntaps - 1;

    work.resize(static_cast<size_t>(h + n));
    if (h) std::memcpy(work.data(), hist.data(), h * sizeof(float));
    std::memcpy(work.data() + h, in, n * sizeof(float));

    std::fill(out, out + n, 0.0f);
    const float* w = work.data();
    for (int k = 0; k < ntaps; ++k)
        axpyf(out, w + (h - k), taps[k], n);

    // history <- last h inputs
    if (h) {
        if (n >= h) {
            std::memcpy(hist.data(), in + (n - h), h * sizeof(float));
        } else {
            std::memmove(hist.data(), hist.data() + n, (h - n) * sizeof(float));
            std::memcpy(hist.data() + (h - n), in, n * sizeof(float));
        }
    }
}

float PALDecoder::dcBlock(float sample)
{
    // Slow DC blocker (~30 Hz). Removes the AM-envelope DC without tilting
    // video lines (previous 0.995 alpha acted like a 10 kHz high-pass and
    // smeared the picture / destabilized sync).
    float out = sample - m_dcBlockerX1 + m_dcBlockAlpha * m_dcBlockerY1;
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
    if (m_amSampleIndex >= m_samplesPerLine * NB_LINES / 2) {
        // Use 95th percentile for max and 5th percentile for min
        // to reject noise spikes. But since we don't track histograms,
        // use a simple approach: shrink the range by 5% on each side
        // to approximate percentile-based AGC.
        float rawRange = m_effMax - m_effMin;
        float newMax = m_effMax - rawRange * 0.02f;
        float newMin = m_effMin + rawRange * 0.02f;
        // Smooth the AGC transition: a hard jump every half frame shifted
        // the sync tip level relative to the threshold and caused periodic
        // sync dropouts. Blend 50% toward the new measurement instead.
        m_ampMax += (newMax - m_ampMax) * 0.5f;
        m_ampMin += (newMin - m_ampMin) * 0.5f;
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
    // Reused per-thread conversion buffer (avoids a heap allocation per call)
    thread_local std::vector<std::complex<float>> samples;
    samples.resize(len / 2);
    for (size_t i = 0; i + 1 < len; i += 2) {
        samples[i / 2] = std::complex<float>(
            static_cast<float>(data[i]) / 128.0f,
            static_cast<float>(data[i + 1]) / 128.0f);
    }
    processSamples(samples);
}

void PALDecoder::processSamples(const std::vector<std::complex<float>>& samples)
{
    QMutexLocker locker(&m_processMutex);
    if (samples.empty() || samples.size() > 100000000) return;

    const std::complex<float>* p = samples.data();
    size_t left = samples.size();
    while (left > 0) {
        const int n = static_cast<int>(std::min<size_t>(left, BLOCK_SAMPLES));
        processBlock(p, n);
        p += n;
        left -= n;
    }
}

void PALDecoder::processBlock(const std::complex<float>* in, int n)
{
    m_blkShiftRe.resize(n); m_blkShiftIm.resize(n);
    m_blkFiltRe.resize(n);  m_blkFiltIm.resize(n);
    m_blkMag.resize(n);
    m_blkVideo.resize(n);
    m_blkLumaIn.resize(n);

    // ---- Pass A: NCO frequency shift (rotating phasor, renormalised every 4096)
    {
        float* sr = m_blkShiftRe.data();
        float* si = m_blkShiftIm.data();
        // State in locals for the whole block (no member round-trips per sample)
        double oscR = m_ncoOsc.real(), oscI = m_ncoOsc.imag();
        const double stR = m_ncoStep.real(), stI = m_ncoStep.imag();
        int renorm = m_ncoRenormCount;
        for (int i = 0; i < n; ++i) {
            const float ncoI = static_cast<float>(oscR);
            const float ncoQ = static_cast<float>(oscI);
            const double nr = oscR * stR - oscI * stI;
            const double ni = oscR * stI + oscI * stR;
            oscR = nr; oscI = ni;
            if (++renorm >= 4096) {
                renorm = 0;
                const double mag = std::sqrt(oscR * oscR + oscI * oscI);
                oscR /= mag; oscI /= mag;
            }
            const float re = in[i].real(), im = in[i].imag();
            sr[i] = re * ncoI - im * ncoQ;
            si[i] = re * ncoQ + im * ncoI;
        }
        m_ncoOsc = std::complex<double>(oscR, oscI);
        m_ncoRenormCount = renorm;
    }

    // ---- Pass B: video IQ low-pass (block FIR) and AM envelope
    blockFir(m_blkShiftRe.data(), m_blkFiltRe.data(), n, m_videoFilterTaps, m_videoHistRe, m_firWork);
    blockFir(m_blkShiftIm.data(), m_blkFiltIm.data(), n, m_videoFilterTaps, m_videoHistIm, m_firWork);
    vecMagnitude(m_blkFiltRe.data(), m_blkFiltIm.data(), m_blkMag.data(), n);

    // ---- Pass C: carrier tracker / AFC / detection / notch / DC / AGC -> video[]
    //      (sequential IIR state, but a tight loop with nothing else in it)
    {
        const float* fr = m_blkFiltRe.data();
        const float* fi = m_blkFiltIm.data();
        const float* mg = m_blkMag.data();
        float* video = m_blkVideo.data();

        // All IIR state in locals for the block: identical arithmetic, but the
        // dependency chains no longer go through memory on every sample.
        float lpR = m_carrLP.real(),   lpI = m_carrLP.imag();
        float pvR = m_carrPrev.real(), pvI = m_carrPrev.imag();
        const float lpC = m_carrLPCoeff;
        double afcR = m_afcAcc.real(), afcI = m_afcAcc.imag();
        double afcSig = m_afcSigPow, afcTot = m_afcTotPow;
        int afcCount = m_afcCount;
        const bool afc = m_afcEnabled, syncDemod = m_syncDemod, vsb = m_vsbComp, invert = m_videoInvert;
        float vsbState = m_vsbLPState;
        const float vsbC = m_vsbLPCoeff;
        const float nb0 = m_notchB0, nb1 = m_notchB1, nb2 = m_notchB2, na1 = m_notchA1, na2 = m_notchA2;
        float nx1 = m_notchX1, nx2 = m_notchX2, ny1 = m_notchY1, ny2 = m_notchY2;
        float dcX1 = m_dcBlockerX1, dcY1 = m_dcBlockerY1;
        const float dcA = m_dcBlockAlpha;

        for (int i = 0; i < n; ++i) {
            const float fR = fr[i], fI = fi[i];
            const float magnitude = mg[i];

            // Carrier tracker: narrowband estimate of the video carrier (DC after
            // the NCO shift). Used for AFC and as phase reference for
            // synchronous detection.   lp += c * (f - lp)
            lpR += lpC * (fR - lpR);
            lpI += lpC * (fI - lpI);
            const float carrNorm = lpR * lpR + lpI * lpI;
            if (afc) {
                // p = lp * conj(prev)
                const float pr = lpR * pvR + lpI * pvI;
                const float pi = lpI * pvR - lpR * pvI;
                afcR += pr; afcI += pi;
                afcSig += carrNorm;
                afcTot += fR * fR + fI * fI;
                if (++afcCount >= m_afcWindow) {
                    // runAfc() works on the members: sync them, run, reload
                    m_afcAcc = std::complex<double>(afcR, afcI);
                    m_afcSigPow = afcSig; m_afcTotPow = afcTot; m_afcCount = afcCount;
                    runAfc();   // new NCO step applies from the next block
                    afcR = m_afcAcc.real(); afcI = m_afcAcc.imag();
                    afcSig = m_afcSigPow; afcTot = m_afcTotPow; afcCount = m_afcCount;
                }
            }
            pvR = lpR; pvI = lpI;

            // Detection: projection on the carrier phase (rejects quadrature
            // noise, no noise-squared bias on weak signals). Falls back to the
            // envelope when no carrier estimate exists yet.
            float detected = magnitude;
            if (syncDemod && carrNorm > 1e-10f) {
                detected = (fR * lpR + fI * lpI) / std::sqrt(carrNorm);
            }

            // Vestigial sideband compensation (Nyquist slope)
            if (vsb) {
                vsbState += vsbC * (detected - vsbState);
                detected -= 0.5f * vsbState;
            }

            // Audio carrier notch filter - remove 5.5 MHz beat
            const float notched = nb0 * detected + nb1 * nx1 + nb2 * nx2 - na1 * ny1 - na2 * ny2;
            nx2 = nx1; nx1 = detected;
            ny2 = ny1; ny1 = notched;

            // Slow DC blocker (~30 Hz), same arithmetic as dcBlock()
            const float dcBlocked = notched - dcX1 + dcA * dcY1;
            dcX1 = notched;
            dcY1 = dcBlocked;

            const float normalized = normalizeAndAGC(dcBlocked);

            // PAL-B negative modulation: invert so sync tips are LOW
            video[i] = invert ? (1.0f - normalized) : normalized;
        }

        m_carrLP = std::complex<float>(lpR, lpI);
        m_carrPrev = std::complex<float>(pvR, pvI);
        m_afcAcc = std::complex<double>(afcR, afcI);
        m_afcSigPow = afcSig; m_afcTotPow = afcTot; m_afcCount = afcCount;
        m_vsbLPState = vsbState;
        m_notchX1 = nx1; m_notchX2 = nx2; m_notchY1 = ny1; m_notchY2 = ny2;
        m_dcBlockerX1 = dcX1; m_dcBlockerY1 = dcY1;
    }

    // ---- Pass D: chroma band-pass on the envelope (block FIR)
    const bool colour = m_colorMode && !m_colorCarrierSin.empty() && !m_chromaFilterTaps.empty();
    if (colour) {
        m_blkChroma.resize(n);
        blockFir(m_blkMag.data(), m_blkChroma.data(), n, m_chromaFilterTaps, m_chromaHist, m_firWork);
    }

    // ---- Pass E: chroma subcarrier notch x2 on the video signal -> luma input
    {
        const float* video = m_blkVideo.data();
        float* luma = m_blkLumaIn.data();
        const float b0 = m_chromaNotchB0, b1 = m_chromaNotchB1, b2 = m_chromaNotchB2, a1 = m_chromaNotchA1, a2 = m_chromaNotchA2;
        float x1 = m_chromaNotchX1, x2 = m_chromaNotchX2, y1 = m_chromaNotchY1, y2 = m_chromaNotchY2;
        float u1 = m_chromaNotch2X1, u2 = m_chromaNotch2X2, w1 = m_chromaNotch2Y1, w2 = m_chromaNotch2Y2;
        for (int i = 0; i < n; ++i) {
            const float v = video[i];
            const float cn1 = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = v;
            y2 = y1; y1 = cn1;
            const float cn2 = b0 * cn1 + b1 * u1 + b2 * u2 - a1 * w1 - a2 * w2;
            u2 = u1; u1 = cn1;
            w2 = w1; w1 = cn2;
            luma[i] = cn2;
        }
        m_chromaNotchX1 = x1; m_chromaNotchX2 = x2; m_chromaNotchY1 = y1; m_chromaNotchY2 = y2;
        m_chromaNotch2X1 = u1; m_chromaNotch2X2 = u2; m_chromaNotch2Y1 = w1; m_chromaNotch2Y2 = w2;
    }

    // ---- Pass F: luma FIR at the decimated rate (same counter as the pixel loop)
    {
        m_blkDecIn.clear();
        int cnt = m_resampleCounter;
        const float* luma = m_blkLumaIn.data();
        for (int i = 0; i < n; ++i) {
            if (++cnt < m_decimFactor) continue;
            cnt = 0;
            m_blkDecIn.push_back(luma[i]);
        }
        m_blkLumaDec.resize(m_blkDecIn.size());
        if (!m_blkDecIn.empty())
            blockFir(m_blkDecIn.data(), m_blkLumaDec.data(), static_cast<int>(m_blkDecIn.size()),
                     m_lumaFilterTaps, m_lumaHist, m_firWork);
    }

    // ---- Pass G: per-sample state machine (sync, burst PLL, chroma demod, pixels)
    {
        const float* video = m_blkVideo.data();
        const float* chromaBandBuf = colour ? m_blkChroma.data() : nullptr;
        const float* lumaDec = m_blkLumaDec.data();
        size_t decIdx = 0;

        // Chroma state that nothing inside processSample() / extractBurstPhase()
        // touches, kept in locals so the per-sample path does not reload members
        // after every (opaque) call.
        const float* lutCos = m_colorCarrierCos.data();
        const float* lutSin = m_colorCarrierSin.data();
        double scPhase = m_scPhase;
        const double scInc = m_scPhaseInc;
        const int burstStart = m_burstStartSample, burstEnd = m_burstEndSample;
        float lpU = m_chromaLPUState, lpV = m_chromaLPVState;
        const float lpC = m_chromaLPCoeff;
        float accU = m_chromaUAccum, accV = m_chromaVAccum;
        float syncLP = m_syncLPState;
        const float syncC = m_syncLPCoeff;
        const int decim = m_decimFactor;
        const int activeStart = m_numberSamplesActiveStart;

        for (int i = 0; i < n; ++i) {
            m_totalSamples++;

            if (++m_statsCountdown >= 10000000) {   // same cadence as (m_totalSamples % 10000000 == 0)
                m_statsCountdown = 0;
                // Flywheel-based sync quality:
                // Component 1: detection rate (0..1) - were sync pulses found?
                // Component 2: error quality (0..1) - how small were flywheel corrections?
                float detectionRate = (m_syncQualityWindow > 0)
                    ? static_cast<float>(m_syncFoundInWindow) / m_syncQualityWindow
                    : 0.0f;
                float avgError = (m_syncFoundInWindow > 0)
                    ? static_cast<float>(m_syncErrorAccum / m_syncFoundInWindow)
                    : static_cast<float>(m_numberSamplesPerHTop);
                float errorQuality = 1.0f - std::clamp(avgError / static_cast<float>(m_numberSamplesPerHTop), 0.0f, 1.0f);
                float syncQuality = (detectionRate * 0.6f + errorQuality * 0.4f) * 100.0f;
                m_lastSyncQuality = syncQuality;
                m_syncQualityWindow = 0;
                m_syncFoundInWindow = 0;
                m_syncErrorAccum = 0.0;
                QMetaObject::invokeMethod(this, [this, syncQuality]() {
                    emit syncStatsUpdated(syncQuality, m_ampMax, m_ampMin);
                }, Qt::QueuedConnection);
            }

            // Sync at full rate (inverted signal: sync tips are LOW), light LPF
            // so ADC noise / chroma ripple don't jitter the comparator.
            syncLP += syncC * (video[i] - syncLP);
            processSample(syncLP);

            // === CHROMA at FULL sample rate ===
            if (colour) {
                int lutIdx = static_cast<int>(scPhase * (SC_LUT_SIZE / (2.0 * M_PI))) & (SC_LUT_SIZE - 1);
                float carrierCos = lutCos[lutIdx];
                float carrierSin = lutSin[lutIdx];
                scPhase += scInc;
                if (scPhase >= 2.0 * M_PI) scPhase -= 2.0 * M_PI;

                const float chromaBand = chromaBandBuf[i];

                // Colour burst PLL: correlate the back-porch burst, then derive
                // the U-axis reference / V switch before active video starts.
                if (m_sampleOffset >= burstStart && m_sampleOffset < burstEnd) {
                    accumulateBurst(chromaBand, carrierCos, carrierSin);
                } else if (m_sampleOffset >= burstEnd && m_burstSampleCount > 0) {
                    extractBurstPhase();
                }

                // Phase-locked product demod on the U / V axes
                float mixU = carrierCos * m_chromaCosRef + carrierSin * m_chromaSinRef;
                float mixV = (carrierSin * m_chromaCosRef - carrierCos * m_chromaSinRef)
                             * PAL_V_AXIS_SIGN;
                float vSign = m_vPhaseAlternate ? -1.0f : 1.0f;
                float uProd = chromaBand * mixU;
                float vProd = chromaBand * mixV * vSign;

                // Post-demod LPF removes the 2*fsc product term
                lpU += lpC * (uProd - lpU);
                lpV += lpC * (vProd - lpV);
                accU += lpU;
                accV += lpV;
            }

            // === Decimate for luma + output chroma ===
            m_resampleCounter++;
            if (m_resampleCounter < decim) continue;
            m_resampleCounter = 0;

            const float luma = (decIdx < m_blkLumaDec.size()) ? lumaDec[decIdx++] : 0.0f;

            // Chroma: average accumulated values over decimation period
            float u = 0.0f, v = 0.0f;
            if (colour) {
                float invDecim = 1.0f / static_cast<float>(decim);
                // Chroma AGC: normalise by the measured burst amplitude so
                // saturation stays constant regardless of RF signal level
                // (0.052 calibrated end-to-end against a synthetic PAL-B source).
                float chromaScale;
                if (!m_chromaMute && m_burstAmpSmoothed > 1e-4f) {
                    chromaScale = invDecim * 0.052f / m_burstAmpSmoothed;
                    float maxScale = (m_ampDelta > 0.001f) ? (invDecim * 10.0f / m_ampDelta)
                                                           : invDecim * 10.0f;
                    if (chromaScale > maxScale) chromaScale = maxScale;
                } else {
                    chromaScale = (m_ampDelta > 0.001f) ? (invDecim * 2.0f / m_ampDelta) : invDecim;
                }
                if (m_chromaMute) chromaScale = 0.0f;  // no burst -> B/W transmission
                u = accU * chromaScale;
                v = accV * chromaScale;
                accU = 0.0f;
                accV = 0.0f;
            }

            // Collect pixels after blanking
            if (m_sampleOffset > activeStart) {
                m_lineBuffer.push_back(luma);
                m_lineBufferU.push_back(u);
                m_lineBufferV.push_back(v);
            }
        }

        m_scPhase = scPhase;
        m_chromaLPUState = lpU; m_chromaLPVState = lpV;
        m_chromaUAccum = accU;  m_chromaVAccum = accV;
        m_syncLPState = syncLP;
    }
}

// ============================================================
// Sync Detection (SDRangel-style)
// ============================================================

void PALDecoder::processSample(float sample)
{
    if (m_autoSync) {
        if (m_sampleOffset >= m_tipStart && m_sampleOffset < m_tipEnd) {
            m_tipSum += sample; m_tipCount++;
        } else if (m_sampleOffset >= m_porchStart && m_sampleOffset < m_porchEnd) {
            m_porchSum += sample; m_porchCount++;
        }
    }

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

                    if (std::fabs(hSyncShift) > m_numberSamplesPerHTop && m_syncLocked) {
                        // Large error WHILE LOCKED: probably a content glitch
                        // or vsync artifact - reject it, but count. Sustained
                        // errors mean the lock is stale: drop it so the
                        // unlocked path below can re-acquire at full strength.
                        m_hSyncErrorCount++;
                        m_syncErrorAccum += static_cast<double>(std::fabs(hSyncShift));
                        if (m_hSyncErrorCount > 20) {
                            m_syncLocked = false;
                            m_syncLockCount = 0;
                            // Stale chroma lines must not be averaged into new ones
                            m_prevLineU.clear();
                            m_prevLineV.clear();
                        }
                    } else {
                        // Good sync: apply correction.
                        // Unlocked: strong gain (0.7) for fast acquisition.
                        // Locked: softer gain + slew limit so noise or video
                        // content can't yank the flywheel and tear the image.
                        // Unlocked + far off: jump the whole distance at
                        // once (acquisition). Otherwise converge smoothly.
                        bool farOff = std::fabs(hSyncShift) > m_numberSamplesPerHTop;
                        float gain = m_syncLocked ? 0.4f : (farOff ? 1.0f : 0.7f);
                        float corr = hSyncShift * gain;
                        if (m_syncLocked) {
                            if (corr >  2.0f) corr =  2.0f;
                            if (corr < -2.0f) corr = -2.0f;
                        }
                        m_hSyncShift = corr;
                        m_hSyncErrorCount = 0;
                        m_syncErrorAccum += static_cast<double>(std::fabs(hSyncShift));
                        if (m_syncLockCount < 1000) m_syncLockCount++;
                        if (m_syncLockCount >= 25) m_syncLocked = true;
                    }

                    m_syncDetected++;
                    m_syncFoundInWindow++;
                    m_sampleOffsetDetected = 0;
#ifdef PAL_TEST_DEBUG
                    { static long sd=0; if (m_sampleRate==16000000) ++sd;
                      if (m_sampleRate==16000000 && sd>=3000 && sd<3012)
                        std::printf("[sync] entryOff=%d shift=%.2f width=%d locked=%d lvl=%.3f spl=%d\n",
                            m_syncPulseEntryOffset, hSyncShift, m_syncPulseCounter, (int)m_syncLocked,
                            m_syncLevel, m_samplesPerLine); }
#endif
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

        // Auto slicer: only adapt while the flywheel is locked (windows are
        // then aligned with the real sync/back porch). Slice halfway between
        // sync tip and black level, smoothed so noise can't make it hunt.
        if (m_autoSync && m_syncLocked && m_tipCount > 0 && m_porchCount > 0) {
            float tip   = static_cast<float>(m_tipSum / m_tipCount);
            float porch = static_cast<float>(m_porchSum / m_porchCount);
            if (porch - tip > 0.05f) {
                float target = tip + 0.5f * (porch - tip);
                m_syncLevel += 0.05f * (target - m_syncLevel);
            }
        }
        m_tipSum = m_porchSum = 0.0;
        m_tipCount = m_porchCount = 0;

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

    m_linesSinceVSync++;

    if (m_vSyncDetectSampleCount > m_vSyncDetectThreshold &&
        (m_lineIndex < 3 || m_lineIndex > VSYNC_LINES + 1) && m_vSyncEnabled)
    {
        // Gate: accept only if the spacing since the last accepted V-sync is
        // ~k * 312.5 lines (k = 1..4, tolerance 5), or it is the tail of the
        // same V-sync (< 8 lines). Isolated noise detections are ignored; after
        // 3 consecutive rejections the lock is considered stale and we re-acquire.
        bool accept = true;
        bool duplicate = false;
        if (m_vSyncValid) {
            const int d = m_linesSinceVSync;
            duplicate = d < 8;
            bool nearMultiple = duplicate;
            for (int k = 1; k <= 4 && !nearMultiple; k++)
                nearMultiple = std::abs(d - static_cast<int>(312.5f * k + 0.5f)) <= 5;
            if (!nearMultiple && m_vSyncRejects < 3) {
                accept = false;
                m_vSyncRejects++;
            }
        }
        if (accept) {
            m_vSyncRejects = 0;
            if (!duplicate) { m_linesSinceVSync = 0; m_vSyncValid = true; }
            if (m_fieldDetectSampleCount > m_fieldDetectThreshold1)
                m_fieldIndex = 0;
            else if (m_fieldDetectSampleCount < m_fieldDetectThreshold2)
                m_fieldIndex = 1;
            m_lineIndex = 2;
        }
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

    // Track lines where the burst window never produced an extraction
    // (vsync lines, flywheel jumps, B/W transmissions). After several such
    // lines, mute chroma so noise doesn't render as rainbow confetti.
    if (m_colorMode && !m_burstSeenThisLine) {
        m_prevBurstValid = false;
        if (m_burstMissCount < 1000) m_burstMissCount++;
        if (m_burstMissCount > 8) m_chromaMute = true;
    }
    m_burstSeenThisLine = false;

    // Blind toggle acts as a PREDICTION of the next line's V switch;
    // extractBurstPhase() overrides it with the measured value (from the
    // burst residual) before active video starts on each colour line.
    m_vPhaseAlternate = !m_vPhaseAlternate;
    // NOTE: Do NOT reset the subcarrier NCO phase here!
    // PAL subcarrier = 283.7516 * fline, so the phase must free-run across
    // lines; the burst PLL measures and corrects the offset each line.

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

    m_curLineU.assign(VIDEO_WIDTH, 0.0f);
    m_curLineV.assign(VIDEO_WIDTH, 0.0f);
    std::vector<float>& currentLineU = m_curLineU;
    std::vector<float>& currentLineV = m_curLineV;

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
                // Chroma shares the luma amplitude scaling: Video Gain is
                // normalised to its default (1.5) so changing it keeps
                // saturation in proportion instead of washing colours out.
                const float chromaK = m_chromaGain * (m_videoGain / 1.5f);
                U *= chromaK;
                V *= chromaK;
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
    m_prevLineU.swap(m_curLineU);
    m_prevLineV.swap(m_curLineV);
}

void PALDecoder::buildFrame()
{
    m_frameCount++;
    const uint8_t* src = m_frameBuffer.data();

    if (m_temporalDenoise) {
        // Motion-adaptive recursive filter: static areas (noise only) are
        // averaged with the previous output (alpha 0.3), moving areas pass
        // through unchanged (alpha -> 1), so no ghosting on motion.
        if (m_denoiseBuf.size() != m_frameBuffer.size())
            m_denoiseBuf = m_frameBuffer;
        const size_t px = m_frameBuffer.size() / 4;
        for (size_t i = 0; i < px; i++) {
            uint8_t* p = &m_denoiseBuf[i * 4];
            const uint8_t* c = &m_frameBuffer[i * 4];
            int db = std::abs(int(c[0]) - int(p[0]));
            int dg = std::abs(int(c[1]) - int(p[1]));
            int dr = std::abs(int(c[2]) - int(p[2]));
            float d = (db + 2 * dg + dr) * 0.25f;
            float a = 0.3f + d * (0.7f / 40.0f);
            if (a > 1.0f) a = 1.0f;
            for (int k = 0; k < 3; k++)
                p[k] = static_cast<uint8_t>(p[k] + (int(c[k]) - int(p[k])) * a);
            p[3] = 255;
        }
        src = m_denoiseBuf.data();
    }

    QImage frame(VIDEO_WIDTH, VIDEO_HEIGHT, QImage::Format_RGB32);
    for (int y = 0; y < VIDEO_HEIGHT; y++) {
        uint8_t* scanLine = frame.scanLine(y);
        std::memcpy(scanLine, &src[y * VIDEO_WIDTH * 4], VIDEO_WIDTH * 4);
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
