#include "PALDecoder.h"
#include <QDebug>
#include <algorithm>
#include <cstring>
#include <cstdio>

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
    m_videoFilterDelay.clear();
    m_lumaFilterDelay.clear();
    m_chromaBandDelay.clear();
    m_chromaLPUState = 0.0f;
    m_chromaLPVState = 0.0f;
    // Post-demod chroma LPF: ~800 kHz (removes the 2*fsc product term)
    m_chromaLPCoeff = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * 8.0e5f / rateF);

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
    m_ncoPhaseIncrement = -2.0 * M_PI * static_cast<double>(m_videoCarrierOffsetHz)
                          / static_cast<double>(m_sampleRate);
    m_ncoPhase = 0.0;
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

float PALDecoder::applyChromaBandFilter(float sample)
{
    m_chromaBandDelay.push_front(sample);
    if (m_chromaBandDelay.size() > m_chromaFilterTaps.size()) m_chromaBandDelay.pop_back();
    float out = 0.0f;
    size_t n = std::min(m_chromaBandDelay.size(), m_chromaFilterTaps.size());
    for (size_t i = 0; i < n; i++) out += m_chromaBandDelay[i] * m_chromaFilterTaps[i];
    return out;
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

        // Sync at full rate (inverted signal: sync tips are now LOW).
        // A light one-pole LPF (~1 MHz) feeds the sync comparator so ADC
        // noise and chroma subcarrier ripple don't jitter the edge timing.
        m_syncLPState += m_syncLPCoeff * (video - m_syncLPState);
        processSample(m_syncLPState);

        // === CHROMA at FULL sample rate (before decimation) ===
        // Demod on raw magnitude — the chroma BPF (bandpass at 4.43 MHz)
        // inherently rejects DC and low-frequency luma content.
        if (m_colorMode && !m_colorCarrierSin.empty()) {
            // Subcarrier NCO: exact free-running phase, no wrap discontinuity
            int lutIdx = static_cast<int>(m_scPhase * (SC_LUT_SIZE / (2.0 * M_PI))) & (SC_LUT_SIZE - 1);
            float carrierCos = m_colorCarrierCos[lutIdx];
            float carrierSin = m_colorCarrierSin[lutIdx];
            m_scPhase += m_scPhaseInc;
            if (m_scPhase >= 2.0 * M_PI) m_scPhase -= 2.0 * M_PI;

            // --- Step 1: extract the chroma band from the envelope ---
            // Band-pass at 4.43 MHz isolates burst + chroma. Burst
            // correlation and demod both use THIS signal, so filter phase
            // is common-mode and cancels out of the colour decode.
            float chromaBand = applyChromaBandFilter(magnitude);

            // --- Step 2: colour burst PLL ---
            // Correlate the back-porch burst against the free-running LUT.
            // extractBurstPhase() derives the U-axis reference and the
            // per-line PAL V-switch BEFORE active video starts, so the rest
            // of the line is demodulated with a phase-locked reference.
            if (m_sampleOffset >= m_burstStartSample && m_sampleOffset < m_burstEndSample) {
#ifdef PAL_TEST_DEBUG
                { static long tl=0;
                  if (m_sampleRate==16000000 && m_lineIndex>=200 && m_lineIndex<=201 && tl<80) { tl++;
                    std::printf("[raw] line=%d off=%d band=%+.5f lutC=%+.3f lutS=%+.3f scPhase=%.4f\n",
                        m_lineIndex, m_sampleOffset, chromaBand, carrierCos, carrierSin, m_scPhase); } }
#endif
                accumulateBurst(chromaBand, carrierCos, carrierSin);
            } else if (m_sampleOffset >= m_burstEndSample && m_burstSampleCount > 0) {
                extractBurstPhase();
            }

            // --- Step 3: phase-locked product demod ---
            //   mixU = cos(phi - alphaU)  (projection onto U axis)
            //   mixV = sin(phi - alphaU)  (projection onto V axis = U + 90 deg)
            // where alphaU comes from the burst mean axis (burst = U + 180 +/- 45).
            float mixU = carrierCos * m_chromaCosRef + carrierSin * m_chromaSinRef;
            float mixV = (carrierSin * m_chromaCosRef - carrierCos * m_chromaSinRef)
                         * PAL_V_AXIS_SIGN;

            float vSign = m_vPhaseAlternate ? -1.0f : 1.0f;
            float uProd = chromaBand * mixU;
            float vProd = chromaBand * mixV * vSign;

            // --- Step 4: post-demod LPF removes the 2*fsc product term ---
            m_chromaLPUState += m_chromaLPCoeff * (uProd - m_chromaLPUState);
            m_chromaLPVState += m_chromaLPCoeff * (vProd - m_chromaLPVState);

            m_chromaUAccum += m_chromaLPUState;
            m_chromaVAccum += m_chromaLPVState;
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
            // Chroma AGC: normalize by the measured burst amplitude so
            // saturation stays constant regardless of RF signal level.
            // (Nominal burst correlation amplitude ~0.075 * video range,
            // so 0.6 / burstAmp matches the old 8 / ampDelta scaling.)
            // Demod math: chromaBand carries C*cos(phi-a); product + LPF
            // yields ~C/2, and the burst correlation yields the burst
            // amplitude scaled by the same chain (including the BPF
            // ramp-up erosion, since the ~2.25 us burst is about as long
            // as the FIR). The 0.052 nominal was calibrated end-to-end
            // against a synthetic PAL-B generator so that decoded U/V
            // amplitudes match the transmitted ones at chromaGain = 1.
            float chromaScale;
            if (!m_chromaMute && m_burstAmpSmoothed > 1e-4f) {
                chromaScale = invDecim * 0.052f / m_burstAmpSmoothed;
                // Clamp so a weak/noisy burst can't blow up the gain
                float maxScale = (m_ampDelta > 0.001f) ? (invDecim * 10.0f / m_ampDelta)
                                                       : invDecim * 10.0f;
                if (chromaScale > maxScale) chromaScale = maxScale;
            } else {
                chromaScale = (m_ampDelta > 0.001f) ? (invDecim * 2.0f / m_ampDelta) : invDecim;
            }
            if (m_chromaMute) chromaScale = 0.0f;  // no burst -> B/W transmission
            u = m_chromaUAccum * chromaScale;
            v = m_chromaVAccum * chromaScale;
#ifdef PAL_TEST_DEBUG
            { static double sU=0,sV=0; static long na=0;
              if (m_sampleOffset > 400 && m_sampleOffset < 900) { sU+=u; sV+=v; na++;
                if (na%200000==0){ std::printf("[chroma] scale=%.2f mute=%d meanU=%+.4f meanV=%+.4f\n",
                    chromaScale,(int)m_chromaMute,sU/na,sV/na); sU=sV=0; na=0; } } }
#endif
            m_chromaUAccum = 0.0f;
            m_chromaVAccum = 0.0f;
        }

        // Collect pixels after blanking
        if (m_sampleOffset > m_numberSamplesActiveStart) {
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
