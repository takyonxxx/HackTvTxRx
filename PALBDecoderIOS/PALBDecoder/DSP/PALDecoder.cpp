#include "PALDecoder.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// PAL colour decode tuning constants
// ----------------------------------------------------------------------------
// PAL_HUE_OFFSET_DEG : global hue trim in degrees. If ALL hues are rotated
//                      (e.g. faces look green), adjust this.
// PAL_VSWITCH_POLARITY: set to 1 if only certain colours look washed out /
//                      gray (e.g. blue-yellow present but red-green missing);
//                      that symptom means the per-line V-switch detection has
//                      inverted polarity on this signal chain.
// PAL_V_AXIS_SIGN    : set to -1.0f if hues are MIRRORED (red and cyan
//                      swapped).
// ============================================================================
static constexpr float PAL_HUE_OFFSET_DEG = 0.0f;
#define PAL_VSWITCH_POLARITY 0
static constexpr float PAL_V_AXIS_SIGN = 1.0f;

static inline float wrapPi(float a) {
    while (a >  (float)M_PI) a -= 2.0f * (float)M_PI;
    while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}

PALDecoder::PALDecoder()
    : m_sampleRate(12500000), m_decimFactor(1), m_decimatedRate(12500000.0f), m_chromaBandwidth(1.2e6f)
    , m_samplesPerLine(800), m_samplesPerLineFrac(0), m_sampleOffset(0), m_sampleOffsetFrac(0)
    , m_sampleOffsetDetected(0), m_hSyncShift(0), m_hSyncErrorCount(0), m_prevSample(0)
    , m_numberSamplesPerHTop(0), m_numberSamplesPerHSync(0), m_numberSamplesPerLineSignals(0)
    , m_lineIndex(0), m_fieldIndex(0)
    , m_fieldDetectStartPos(0), m_fieldDetectEndPos(0), m_vSyncDetectStartPos(0), m_vSyncDetectEndPos(0)
    , m_fieldDetectSampleCount(0), m_vSyncDetectSampleCount(0)
    , m_vSyncDetectThreshold(0), m_fieldDetectThreshold1(0), m_fieldDetectThreshold2(0)
    , m_ampMin(-1), m_ampMax(1), m_ampDelta(2), m_effMin(20), m_effMax(-20)
    , m_videoGain(1.5f), m_videoOffset(0), m_videoInvert(true), m_colorMode(false)
    , m_syncLevel(0.0f), m_chromaGain(0.75f), m_hSyncEnabled(true), m_vSyncEnabled(true)
    , m_totalSamples(0), m_frameCount(0), m_linesProcessed(0), m_syncDetected(0)
    , m_syncQualityWindow(0), m_syncFoundInWindow(0), m_syncErrorAccum(0), m_lastSyncQuality(0)
    , m_vPhaseAlternate(false)
{
    m_frameBuffer.resize(VIDEO_WIDTH * VIDEO_HEIGHT * 4, 0);
    m_lineBuffer.reserve(2048); m_lineBufferU.reserve(2048); m_lineBufferV.reserve(2048);
    for (int i = 0; i < NCO_LUT_SIZE; i++) {
        double ph = 2.0 * M_PI * i / NCO_LUT_SIZE;
        m_ncoSin[i] = (float)sin(ph); m_ncoCos[i] = (float)cos(ph);
    }
    setSampleRate(12500000);
    updateNCO();
}

PALDecoder::~PALDecoder() {}

void PALDecoder::setSampleRate(int sr) {
    std::lock_guard<std::mutex> lock(m_processMutex);
    m_sampleRate = sr;
    if (sr >= 16000000) m_decimFactor = 4;
    else if (sr >= 8000000) m_decimFactor = 2;
    else m_decimFactor = 1;
    m_decimatedRate = (float)sr / m_decimFactor;
    m_chromaBandwidth = (sr >= 16000000) ? 1.5e6f : 1.2e6f;
    applyStandard(); initFilters(); rebuildColorLUT(); initBurstPLL(); updateNCO();
    m_resampleCounter = 0; m_sampleOffset = 0; m_sampleOffsetFrac = 0;
    m_hSyncShift = 0; m_hSyncErrorCount = 0; m_lineIndex = 0; m_fieldIndex = 0;
    m_ampMin = -1; m_ampMax = 1; m_ampDelta = 2; m_effMin = 20; m_effMax = -20; m_amSampleIndex = 0;
    m_ncoAccum = 0;
    m_scAccum = 0;

    float rateF = (float)sr;
    // Sync path one-pole LPF: ~1 MHz cutoff smooths ADC noise on the
    // threshold comparator without meaningfully delaying the sync edge.
    m_syncLPCoeff = 1.0f - expf(-2.0f * (float)M_PI * 1.0e6f / rateF);
    m_syncLPState = 0.5f;
    m_syncLockCount = 0; m_syncLocked = false;
    m_syncPulseActive = false; m_syncPulseCounter = 0;

    // Post-demod chroma LPF: ~800 kHz (removes the 2*fsc product term)
    m_chromaLPCoeff = 1.0f - expf(-2.0f * (float)M_PI * 8.0e5f / rateF);
    m_chromaLPUState = 0; m_chromaLPVState = 0;

    // Reset burst PLL
    m_burstCorrI = 0; m_burstCorrQ = 0;
    m_burstDCAccum = 0; m_burstCosAccum = 0; m_burstSinAccum = 0;
    m_burstSampleCount = 0;
    m_burstAmplitude = 0; m_burstValid = false;
    m_chromaRefPhase = 0; m_chromaCosRef = 1.0f; m_chromaSinRef = 0.0f;
    m_burstAmpSmoothed = 0.04f;
    m_burstMeanInit = false; m_burstMeanPhase = 0;
    m_prevBurstAngle = 0; m_prevBurstValid = false;
    m_burstSeenThisLine = false; m_burstMissCount = 0;
    m_chromaMute = true;
}

void PALDecoder::applyStandard() {
    float r = (float)m_sampleRate, spl = r / (NB_LINES * FPS);
    m_samplesPerLine = (int)spl; m_samplesPerLineFrac = spl - m_samplesPerLine;
    m_numberSamplesPerHTop = (int)(SYNC_PULSE_FRAC * spl);
    m_numberSamplesPerHSync = (int)(HSYNC_FRAC * spl);
    m_numberSamplesPerLineSignals = (int)(BLANKING_FRAC * spl);
    m_fieldDetectStartPos = (int)(FIELD_DETECT_START * spl);
    m_fieldDetectEndPos = (int)(FIELD_DETECT_END * spl);
    m_vSyncDetectStartPos = (int)((FIELD_DETECT_START + HALF_LINE) * spl);
    m_vSyncDetectEndPos = (int)((FIELD_DETECT_END + HALF_LINE) * spl);
    // Sync pulse width validation limits: real H-sync is 4.7 us.
    // Accept 3.0-6.5 us; rejects video dips, vsync equalizing pulses
    // (2.35 us) and vsync broad pulses (~27 us).
    m_syncPulseMinWidth = (int)(3.0f / 64.0f * spl);
    m_syncPulseMaxWidth = (int)(6.5f / 64.0f * spl);
    float dtl = (FIELD_DETECT_END - FIELD_DETECT_START) * spl;
    m_fieldDetectThreshold1 = (int)(dtl * 0.75f);
    m_fieldDetectThreshold2 = (int)(dtl * 0.25f);
    m_vSyncDetectThreshold = (int)(dtl * 0.5f);
}

void PALDecoder::initFilters() {
    float r = (float)m_sampleRate;
    float vc = std::min(5.5e6f, r * 0.4f);
    m_videoFilterTaps = designLowPassFIR(vc, r, 15);
    m_vidFirI.setLen(15); m_vidFirQ.setLen(15);
    float lc = std::min(5.0e6f, m_decimatedRate * 0.45f);
    m_lumaFilterTaps = designLowPassFIR(lc, m_decimatedRate, 17);
    m_lumaFir.setLen(17);
    if (COLOR_CARRIER_FREQ < r / 2.0f) {
        // Chroma BPF at FULL sample rate (chroma demod runs before
        // decimation). More taps at higher rates for a sharper filter.
        int chromaTaps;
        if (m_sampleRate >= 20000000)      chromaTaps = 45;
        else if (m_sampleRate >= 16000000) chromaTaps = 35;
        else                               chromaTaps = 31;
        m_chromaFilterTaps = designBandPassFIR(COLOR_CARRIER_FREQ, m_chromaBandwidth, r, chromaTaps);
        m_chromaFirBand.setLen(chromaTaps);
    } else { m_chromaFilterTaps.clear(); m_chromaFirBand.setLen(1); }
}

void PALDecoder::rebuildColorLUT() {
    float r = (float)m_sampleRate;
    if (COLOR_CARRIER_FREQ >= r/2) { m_scStep = 0; m_scAccum = 0; return; }
    // Exact free-running phase accumulator: 32-bit fraction of one cycle,
    // indexing the shared one-cycle sin/cos LUT (m_ncoSin/m_ncoCos).
    // No wrap discontinuity, no per-line reset - the burst PLL measures
    // and corrects the residual phase every line.
    m_scStep = (uint32_t)(int64_t)((double)COLOR_CARRIER_FREQ / (double)r * 4294967296.0);
    m_scAccum = 0;
}

void PALDecoder::initBurstPLL() {
    float rate = (float)m_sampleRate;
    // PAL-B colour burst: 10 +/- 1 cycles of 4.43 MHz subcarrier in the
    // back porch, ~5.6..7.85 us after the H-sync leading edge (full rate).
    float exactSPL = rate / (NB_LINES * FPS);
    // Burst is correlated on the SAME band-filtered signal used for chroma
    // demodulation, so the linear-phase BPF group delay ((taps-1)/2)
    // shifts the burst window by the same amount and the phase reference
    // stays exactly consistent with the demod path.
    int groupDelay = m_chromaFilterTaps.empty()
        ? 0 : ((int)m_chromaFilterTaps.size() - 1) / 2;
    m_burstStartSample = (int)(5.6f / 64.0f * exactSPL) + groupDelay;
    m_burstEndSample   = (int)(7.85f / 64.0f * exactSPL) + groupDelay;
}

void PALDecoder::accumulateBurst(float sample, float cosVal, float sinVal) {
    m_burstCorrI += sample * cosVal;
    m_burstCorrQ += sample * sinVal;
    m_burstDCAccum += sample;
    m_burstCosAccum += cosVal;
    m_burstSinAccum += sinVal;
    m_burstSampleCount++;
}

void PALDecoder::extractBurstPhase() {
    int count = m_burstSampleCount;
    float N = (float)count;

    // Remove DC bias from the correlation:
    //   sum(sample * cos) = DC * sum(cos) + sum(AC * cos)
    float dcMean = (count > 0) ? (m_burstDCAccum / N) : 0.0f;
    float corrI = (count > 0) ? ((m_burstCorrI - dcMean * m_burstCosAccum) / N) : 0.0f;
    float corrQ = (count > 0) ? ((m_burstCorrQ - dcMean * m_burstSinAccum) / N) : 0.0f;

    // Consume accumulators so this runs once per line
    m_burstCorrI = 0; m_burstCorrQ = 0;
    m_burstDCAccum = 0; m_burstCosAccum = 0; m_burstSinAccum = 0;
    m_burstSampleCount = 0;
    m_burstSeenThisLine = true;

    float amplitude = sqrtf(corrI * corrI + corrQ * corrQ);

    // Relative validity threshold: burst must be a meaningful fraction of
    // the video amplitude range (rejects noise on B/W or vsync lines).
    float minAmp = 0.008f * m_ampDelta;
    if (minAmp < 0.0005f) minAmp = 0.0005f;

    if (count < 8 || amplitude < minAmp) {
        m_burstValid = false;
        m_prevBurstValid = false;
        if (m_burstMissCount < 1000) m_burstMissCount++;
        if (m_burstMissCount > 8) m_chromaMute = true;
        return;
    }

    m_burstValid = true;
    m_burstAmplitude = amplitude;
    m_burstMissCount = 0;

    // Smooth burst amplitude for chroma AGC
    m_burstAmpSmoothed = m_burstAmpSmoothed * 0.9f + amplitude * 0.1f;

    // ---- Swinging-burst mean-axis tracking ----
    // The correlation returns the burst vector's angle in the LUT
    // (cos, sin) plane. PAL burst sits at (U-axis + 180 +/- 45 deg),
    // alternating each line. Vector-averaging two CONSECUTIVE line bursts
    // cancels the +/-45 swing and yields the mean burst axis
    // (= U axis + 180 deg) directly.
    float measuredAngle = atan2f(corrQ, corrI);

    if (m_prevBurstValid) {
        float mx = cosf(measuredAngle) + cosf(m_prevBurstAngle);
        float my = sinf(measuredAngle) + sinf(m_prevBurstAngle);
        // If the two vectors are near-opposite the mean is undefined; skip.
        if (mx * mx + my * my > 0.1f) {
            float meanTarget = atan2f(my, mx);
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
    // the blind every-line toggle (which loses phase across dropouts).
    float resid = wrapPi(measuredAngle - m_burstMeanPhase);
    bool vInverted = (resid > 0.0f);
#if PAL_VSWITCH_POLARITY
    vInverted = !vInverted;
#endif
    m_vPhaseAlternate = vInverted;

    // ---- U-axis reference ----
    // U axis = mean burst axis - 180 deg (+ optional hue trim).
    float alphaU = wrapPi(m_burstMeanPhase - (float)M_PI
                          + PAL_HUE_OFFSET_DEG * (float)M_PI / 180.0f);
    m_chromaRefPhase = alphaU;
    m_chromaCosRef = cosf(alphaU);
    m_chromaSinRef = sinf(alphaU);

    m_chromaMute = false;
}

void PALDecoder::setTuneFrequency(uint64_t f) { m_tuneFrequency = f; updateNCO(); }

void PALDecoder::updateNCO() {
    double t = m_tuneFrequency / 1e6, vc;
    if (t >= 470 && t <= 862) { int n = (int)floor((t-470+0.5)/8); if(n<0)n=0; vc = 470+n*8.0+1.25; }
    else if (t >= 174 && t <= 230) { int n = (int)floor((t-174+0.5)/8); if(n<0)n=0; vc = 174+n*8.0+1.25; }
    else vc = t;
    m_videoCarrierOffsetHz = (float)((vc - t) * 1e6);
    double norm = (double)m_videoCarrierOffsetHz / (double)m_sampleRate;
    m_ncoStep = (uint32_t)(int64_t)(-norm * 4294967296.0);
    m_ncoAccum = 0;
}

std::vector<float> PALDecoder::designLowPassFIR(float cutoff, float sr, int n) {
    std::vector<float> t(n); float fc = cutoff/sr; int M = n-1;
    for (int i = 0; i < n; i++) {
        float mm = i-M/2.0f;
        float h = (mm==0) ? 2*fc : sinf(2*M_PI*fc*mm)/(M_PI*mm);
        t[i] = h * (0.42f-0.5f*cosf(2*M_PI*i/M)+0.08f*cosf(4*M_PI*i/M));
    }
    float s = 0; for (auto v:t) s+=v; if(s!=0) for(auto&v:t) v/=s;
    return t;
}

std::vector<float> PALDecoder::designBandPassFIR(float cf, float bw, float sr, int n) {
    // LOW-PASS prototype (cutoff = bw/2), then modulate to cf.
    // NOTE: the previous version used a band-pass difference-of-sincs
    // kernel here AND modulated it by cos again, which double-shifted the
    // band to DC and 2*fc. Combined with the passband-at-fc normalization
    // this gave the "band-pass" a DC gain of ~20, so the envelope's DC
    // leaked into the chroma path far stronger than the actual subcarrier.
    std::vector<float> t(n); float fc=cf/sr, b=bw/sr/2; int M=n-1;
    for (int i=0;i<n;i++) {
        float mm=i-M/2.0f;
        float h=(mm==0)?2*b:sinf(2*M_PI*b*mm)/(M_PI*mm);
        float w=0.42f-0.5f*cosf(2*M_PI*i/M)+0.08f*cosf(4*M_PI*i/M);
        t[i]=h*w*2*cosf(2*M_PI*fc*mm);
    }
    float s=0; for(int i=0;i<n;i++){float mm=i-(n-1)/2.0f;s+=t[i]*cosf(2*M_PI*fc*mm);}
    if(fabsf(s)>1e-6f) for(auto&v:t) v/=fabsf(s);
    return t;
}

float PALDecoder::normalizeAndAGC(float s) {
    if(s<m_effMin)m_effMin=s; if(s>m_effMax)m_effMax=s;
    if(++m_amSampleIndex >= m_samplesPerLine*NB_LINES/2) {
        float rawRange=m_effMax-m_effMin;
        float newMax=m_effMax-rawRange*0.02f;
        float newMin=m_effMin+rawRange*0.02f;
        m_ampMax+=(newMax-m_ampMax)*0.5f;
        m_ampMin+=(newMin-m_ampMin)*0.5f;
        m_ampDelta=m_ampMax-m_ampMin;
        if(m_ampDelta<=0.001f)m_ampDelta=1;
        m_effMin=20;m_effMax=-20;m_amSampleIndex=0;
    }
    float n=(s-m_ampMin)/m_ampDelta;
    return n>1?1:n;
}

static inline float fastMag(float i, float q) {
    float ai = fabsf(i), aq = fabsf(q);
    return (ai > aq) ? ai + 0.4f * aq : aq + 0.4f * ai;
}

void PALDecoder::processSamples(const int8_t* data, size_t len) {
    if(!data||len==0) return;
    std::lock_guard<std::mutex> lock(m_processMutex);
    const bool doColor = m_colorMode && m_scStep != 0;
    const float invD = 1.0f / m_decimFactor;
    const float* lTaps = m_lumaFilterTaps.data();
    const size_t half = len / 2;

    float ampMin = m_ampMin, ampDelta = m_ampDelta;
    float effMin = m_effMin, effMax = m_effMax;
    int amIdx = m_amSampleIndex;
    const int agcPeriod = m_samplesPerLine * NB_LINES / 2;
    float ampMax = m_ampMax;
    float prevSample = m_prevSample;
    const float syncLevel = m_syncLevel;
    const int spl = m_samplesPerLine;
    const int hTop = m_numberSamplesPerHTop;
    const int hSync = m_numberSamplesPerHSync;
    const float* ncoC = m_ncoCos;
    const float* ncoS = m_ncoSin;

    float accumI = 0, accumQ = 0;
    const float invAmpDelta = (ampDelta > 0) ? 1.0f / ampDelta : 1.0f;

    for (size_t i = 0; i < half; i++) {
        float sI = data[i*2] * (1.0f/128.0f);
        float sQ = data[i*2+1] * (1.0f/128.0f);

        uint32_t idx = m_ncoAccum >> (32 - NCO_LUT_BITS);
        float cI = ncoC[idx], sN = ncoS[idx];
        m_ncoAccum += m_ncoStep;
        float shI = sI*cI - sQ*sN;
        float shQ = sI*sN + sQ*cI;

        float ai = fabsf(shI), aq = fabsf(shQ);
        float mag = (ai > aq) ? ai + 0.4f * aq : aq + 0.4f * ai;

        if (mag < effMin) effMin = mag;
        if (mag > effMax) effMax = mag;
        if (++amIdx >= agcPeriod) {
            // Smooth the AGC transition: a hard jump shifted the sync tip
            // level relative to the threshold and caused periodic sync
            // dropouts. Blend 50% toward the new measurement instead, and
            // shrink the range 2% per side to reject noise spikes.
            float rawRange = effMax - effMin;
            float newMax = effMax - rawRange * 0.02f;
            float newMin = effMin + rawRange * 0.02f;
            ampMax += (newMax - ampMax) * 0.5f;
            ampMin += (newMin - ampMin) * 0.5f;
            ampDelta = ampMax - ampMin;
            if (ampDelta <= 0.001f) ampDelta = 1;
            effMin = 20; effMax = -20; amIdx = 0;
        }
        float norm = (mag - ampMin) * invAmpDelta;
        if (norm > 1) norm = 1; else if (norm < 0) norm = 0;

        // === Sync: LPF + pulse-width validation + locked flywheel ===
        // A light one-pole LPF (~1 MHz) feeds the comparator so ADC noise
        // and chroma subcarrier ripple don't jitter the edge timing.
        m_syncLPState += m_syncLPCoeff * (norm - m_syncLPState);
        // Sync tips must be LOW at the comparator (negative modulation
        // demodulates sync as the HIGHEST envelope level).
        float syncVid = m_videoInvert ? (1.0f - m_syncLPState) : m_syncLPState;

        if (syncVid < syncLevel) {
            if (!m_syncPulseActive) {
                // Leading edge: record position (fractional interpolation)
                m_syncPulseActive = true;
                m_syncPulseCounter = 1;
                float denom = prevSample - syncVid;
                m_syncPulseEntryFrac = (denom > 1e-6f)
                    ? (prevSample - syncLevel) / denom : 0.0f;
                m_syncPulseEntryOffset = m_sampleOffset;
                m_syncPulseEntryOffsetFrac = m_sampleOffsetFrac;
            } else {
                m_syncPulseCounter++;
                // Too wide -> not H-sync (probably a vsync broad pulse)
                if (m_syncPulseCounter > m_syncPulseMaxWidth) {
                    m_syncPulseActive = false;
                    m_syncPulseCounter = 0;
                }
            }
        } else if (m_syncPulseActive) {
            // Trailing edge: validate width
            if (m_syncPulseCounter >= m_syncPulseMinWidth
                && m_syncPulseCounter <= m_syncPulseMaxWidth
                && m_sampleOffsetDetected > spl - hTop)
            {
                float hs = -((float)m_syncPulseEntryOffset
                             + m_syncPulseEntryOffsetFrac + m_syncPulseEntryFrac);
                if (hs > spl/2) hs -= spl;
                else if (hs < -spl/2) hs += spl;

                if (fabsf(hs) > hTop && m_syncLocked) {
                    // Large error WHILE LOCKED: probably a glitch. Reject,
                    // but if sustained, drop the lock so the unlocked path
                    // can re-acquire at full strength.
                    m_hSyncErrorCount++;
                    m_syncErrorAccum += fabsf(hs);
                    if (m_hSyncErrorCount > 20) {
                        m_syncLocked = false;
                        m_syncLockCount = 0;
                    }
                } else {
                    // Unlocked + far off: jump the whole distance at once
                    // (acquisition). Locked: soft gain + slew limit so
                    // content can't yank the flywheel and tear the image.
                    bool farOff = fabsf(hs) > hTop;
                    float gain = m_syncLocked ? 0.4f : (farOff ? 1.0f : 0.7f);
                    float corr = hs * gain;
                    if (m_syncLocked) {
                        if (corr >  2.0f) corr =  2.0f;
                        if (corr < -2.0f) corr = -2.0f;
                    }
                    m_hSyncShift = corr;
                    m_hSyncErrorCount = 0;
                    m_syncErrorAccum += fabsf(hs);
                    if (m_syncLockCount < 1000) m_syncLockCount++;
                    if (m_syncLockCount >= 25) m_syncLocked = true;
                }
                m_syncDetected++; m_syncFoundInWindow++;
                m_sampleOffsetDetected = 0;
            }
            m_syncPulseActive = false;
            m_syncPulseCounter = 0;
        }
        m_sampleOffsetDetected++;
        prevSample = syncVid;

        m_sampleOffset++;
        // VSync counters must look at the same INVERTED domain as H-sync:
        // with negative modulation, raw norm has sync HIGH and video LOW,
        // so comparing raw norm against the threshold counted normal video
        // as "sync" on every line and false-triggered the vsync reset.
        if (m_sampleOffset > m_fieldDetectStartPos && m_sampleOffset < m_fieldDetectEndPos)
            m_fieldDetectSampleCount += (syncVid < syncLevel) ? 1 : 0;
        if (m_sampleOffset > m_vSyncDetectStartPos && m_sampleOffset < m_vSyncDetectEndPos)
            m_vSyncDetectSampleCount += (syncVid < syncLevel) ? 1 : 0;

        if (m_sampleOffset >= spl) {
            float sof = m_hSyncShift + m_sampleOffsetFrac - m_samplesPerLineFrac;
            m_sampleOffset = (int)sof; m_sampleOffsetFrac = sof - m_sampleOffset; m_hSyncShift = 0;
            m_lineIndex++; m_linesProcessed++; m_syncQualityWindow++;
            processEndOfLine();
            m_totalSamples += spl;
            if (__builtin_expect(m_totalSamples >= 10000000, 0)) {
                float dr = m_syncQualityWindow > 0 ? (float)m_syncFoundInWindow / m_syncQualityWindow : 0;
                float ae = m_syncFoundInWindow > 0 ? (float)(m_syncErrorAccum / m_syncFoundInWindow) : (float)hTop;
                float eq = 1 - std::clamp(ae / (float)hTop, 0.0f, 1.0f);
                m_lastSyncQuality = (dr * 0.6f + eq * 0.4f) * 100;
                m_syncQualityWindow = 0; m_syncFoundInWindow = 0; m_syncErrorAccum = 0;
                m_totalSamples = 0;
                if (m_syncStatsCallback) m_syncStatsCallback(m_lastSyncQuality, ampMax, ampMin);
            }
        }

        accumI += shI; accumQ += shQ;

        if (doColor) {
            // Free-running subcarrier NCO (no per-line reset!)
            uint32_t scIdx = m_scAccum >> (32 - NCO_LUT_BITS);
            float cc = ncoC[scIdx], cs = ncoS[scIdx];
            m_scAccum += m_scStep;

            // --- Step 1: extract the chroma band from the envelope ---
            // Band-pass at 4.43 MHz isolates burst + chroma. Burst
            // correlation and demod both use THIS signal, so filter phase
            // is common-mode and cancels out of the colour decode.
            m_chromaFirBand.push(mag);
            float chromaBand = m_chromaFirBand.apply(m_chromaFilterTaps.data());

            // --- Step 2: colour burst PLL ---
            // Correlate the back-porch burst against the free-running LUT;
            // extractBurstPhase() derives the U-axis reference and the
            // per-line PAL V-switch before active video starts.
            if (m_sampleOffset >= m_burstStartSample && m_sampleOffset < m_burstEndSample) {
                accumulateBurst(chromaBand, cc, cs);
            } else if (m_sampleOffset >= m_burstEndSample && m_burstSampleCount > 0) {
                extractBurstPhase();
            }

            // --- Step 3: phase-locked product demod ---
            //   mixU = cos(phi - alphaU)   (projection onto U axis)
            //   mixV = sin(phi - alphaU)   (V axis = U + 90 deg)
            float mixU = cc * m_chromaCosRef + cs * m_chromaSinRef;
            float mixV = (cs * m_chromaCosRef - cc * m_chromaSinRef) * PAL_V_AXIS_SIGN;

            float vSign = m_vPhaseAlternate ? -1.0f : 1.0f;
            float uProd = chromaBand * mixU;
            float vProd = chromaBand * mixV * vSign;

            // --- Step 4: post-demod LPF removes the 2*fsc product term ---
            m_chromaLPUState += m_chromaLPCoeff * (uProd - m_chromaLPUState);
            m_chromaLPVState += m_chromaLPCoeff * (vProd - m_chromaLPVState);

            m_chromaUAccum += m_chromaLPUState;
            m_chromaVAccum += m_chromaLPVState;
        }

        if (++m_resampleCounter < m_decimFactor) continue;
        m_resampleCounter = 0;

        float avgI = accumI * invD, avgQ = accumQ * invD;
        accumI = 0; accumQ = 0;
        float dMag = fastMag(avgI, avgQ);
        float dNorm = (dMag - ampMin) * invAmpDelta;
        if (dNorm > 1) dNorm = 1; else if (dNorm < 0) dNorm = 0;

        m_lumaFir.push(dNorm);
        float luma = m_lumaFir.apply(lTaps);
        float u = 0, v = 0;
        if (doColor) {
            // Chroma AGC: normalize saturation by the measured burst
            // amplitude so colour stays constant regardless of RF level.
            // The 0.11 nominal was calibrated end-to-end against a
            // synthetic PAL-B generator (decoded U/V match transmitted
            // amplitudes at chromaGain = 1). Note: this constant is
            // chain-specific - the desktop build uses 0.052 because its
            // video IQ LPF + sqrt magnitude give a different burst/chroma
            // amplitude ratio than this raw-envelope fastMag chain.
            float chromaScale;
            if (!m_chromaMute && m_burstAmpSmoothed > 1e-4f) {
                chromaScale = invD * 0.11f / m_burstAmpSmoothed;
                float maxScale = (ampDelta > 0.001f) ? (invD * 10.0f / ampDelta)
                                                     : invD * 10.0f;
                if (chromaScale > maxScale) chromaScale = maxScale;
            } else {
                chromaScale = (ampDelta > 0.001f) ? (invD * 2.0f / ampDelta) : invD;
            }
            if (m_chromaMute) chromaScale = 0.0f;  // no burst -> B/W transmission
            u = m_chromaUAccum * chromaScale;
            v = m_chromaVAccum * chromaScale;
            m_chromaUAccum = 0; m_chromaVAccum = 0;
        }
        if (m_sampleOffset > hSync) {
            m_lineBuffer.push_back(luma);
            m_lineBufferU.push_back(u);
            m_lineBufferV.push_back(v);
        }
    }

    m_ampMin = ampMin; m_ampMax = ampMax; m_ampDelta = ampDelta;
    m_effMin = effMin; m_effMax = effMax; m_amSampleIndex = amIdx;
    m_prevSample = prevSample;
}

void PALDecoder::processSamples(const std::vector<std::complex<float>>& s) {
    if(s.empty())return;
    std::vector<int8_t> b(s.size()*2);
    for(size_t i=0;i<s.size();i++){
        b[i*2]=(int8_t)std::clamp(s[i].real()*128,-127.0f,127.0f);
        b[i*2+1]=(int8_t)std::clamp(s[i].imag()*128,-127.0f,127.0f);
    }
    processSamples(b.data(),b.size());
}

void PALDecoder::processSample(float s) {
    m_prevSample=s;
}

void PALDecoder::processEndOfLine(){
    if(m_lineIndex==VSYNC_LINES+3&&m_fieldIndex==0) buildFrame();
    if(m_vSyncDetectSampleCount>m_vSyncDetectThreshold&&(m_lineIndex<3||m_lineIndex>VSYNC_LINES+1)&&m_vSyncEnabled){
        if(m_fieldDetectSampleCount>m_fieldDetectThreshold1)m_fieldIndex=0;
        else if(m_fieldDetectSampleCount<m_fieldDetectThreshold2)m_fieldIndex=1;
        m_lineIndex=2;
    }
    m_fieldDetectSampleCount=0;m_vSyncDetectSampleCount=0;
    if(m_lineIndex>NB_LINES/2+m_fieldIndex){m_lineIndex=1;m_fieldIndex=1-m_fieldIndex;}
    renderLine();
    m_lineBuffer.clear();m_lineBufferU.clear();m_lineBufferV.clear();

    // Track lines where the burst window never produced an extraction
    // (vsync lines, flywheel jumps, B/W transmissions). After several such
    // lines, mute chroma so noise doesn't render as rainbow confetti.
    if (m_colorMode && !m_burstSeenThisLine) {
        m_prevBurstValid = false;
        if (m_burstMissCount < 1000) m_burstMissCount++;
        if (m_burstMissCount > 8) m_chromaMute = true;
    }
    m_burstSeenThisLine = false;

    // Blind toggle is a PREDICTION of the next line's V switch;
    // extractBurstPhase() overrides it with the measured value before
    // active video starts on each colour line.
    m_vPhaseAlternate = !m_vPhaseAlternate;
    // NOTE: subcarrier NCO phase is NOT reset here. PAL subcarrier
    // = 283.7516 * fline, so the phase must free-run across lines;
    // the burst PLL measures and corrects the offset each line.

    // Reset burst PLL per-line accumulators
    m_burstCorrI = 0; m_burstCorrQ = 0;
    m_burstDCAccum = 0; m_burstCosAccum = 0; m_burstSinAccum = 0;
    m_burstSampleCount = 0;
}

// Optimized renderLine: fixed-point scaling, no float interpolation in B&W mode,
// integer YUV->RGB, XOR invert
void PALDecoder::renderLine(){
    int row=(m_lineIndex-FIRST_VISIBLE_LINE)*2-m_fieldIndex;
    if(row<0||row>=VIDEO_HEIGHT)return;
    const int as=(int)m_lineBuffer.size(); if(as<10)return;
    uint8_t* __restrict__ rp=m_frameBuffer.data()+row*VIDEO_WIDTH*4;
    const float* __restrict__ lb = m_lineBuffer.data();

    // Fixed-point scale: 16.16 format
    const uint32_t scaleFixed = (uint32_t)(((uint64_t)as << 16) / VIDEO_WIDTH);
    const float vGain = m_videoGain;
    const float vOff = m_videoOffset;
    const bool doInvert = m_videoInvert;
    // XOR mask: 0xFF for invert, 0x00 for normal
    const uint8_t xorMask = doInvert ? 0xFF : 0x00;

    if (!m_colorMode) {
        // B&W fast path: nearest-neighbor, integer math, no chroma
        for (int x = 0; x < VIDEO_WIDTH; x++) {
            uint32_t srcFixed = (uint32_t)x * scaleFixed;
            int idx = srcFixed >> 16;
            if (idx >= as) idx = as - 1;
            float Y = lb[idx] * vGain + vOff;
            // Clamp and convert to byte in one step
            int yByte = (int)(Y * 255.0f);
            if (yByte < 0) yByte = 0; else if (yByte > 255) yByte = 255;
            uint8_t val = (uint8_t)yByte ^ xorMask;
            rp[x*4] = val; rp[x*4+1] = val; rp[x*4+2] = val; rp[x*4+3] = 255;
        }
    } else {
        // Color path with interpolation.
        // NOTE: video inversion must be applied to LUMA ONLY, before the
        // YUV->RGB math. The old XOR-on-RGB approach also negated the
        // chroma contribution (255-R flips the sign of V inside R), which
        // rotated every hue by 180 degrees.
        const float sc = (float)as / VIDEO_WIDTH;
        for (int x = 0; x < VIDEO_WIDTH; x++) {
            float sx = x * sc; int idx = (int)sx; float fr = sx - idx;
            float Y = lb[idx]; if (idx+1 < as) Y += (lb[idx+1] - Y) * fr;
            if (doInvert) Y = 1.0f - Y;
            Y = Y * vGain + vOff;
            if (Y < 0) Y = 0; else if (Y > 1) Y = 1;
            float U = 0, V = 0;
            if (idx < (int)m_lineBufferU.size()) {
                int i2 = std::min(idx+1, (int)m_lineBufferU.size()-1);
                U = m_lineBufferU[idx] + (m_lineBufferU[i2] - m_lineBufferU[idx]) * fr;
                V = m_lineBufferV[idx] + (m_lineBufferV[i2] - m_lineBufferV[idx]) * fr;
                if (!m_prevLineU.empty() && x < (int)m_prevLineU.size()) {
                    U = (U + m_prevLineU[x]) * 0.5f;
                    V = (V + m_prevLineV[x]) * 0.5f;
                }
                U *= m_chromaGain; V *= m_chromaGain;
            }
            int R = (int)((Y + 1.14f * V) * 255);
            int G = (int)((Y - 0.396f * U - 0.581f * V) * 255);
            int B = (int)((Y + 2.029f * U) * 255);
            if (R < 0) R = 0; else if (R > 255) R = 255;
            if (G < 0) G = 0; else if (G > 255) G = 255;
            if (B < 0) B = 0; else if (B > 255) B = 255;
            rp[x*4]   = (uint8_t)R;
            rp[x*4+1] = (uint8_t)G;
            rp[x*4+2] = (uint8_t)B;
            rp[x*4+3] = 255;
        }
        m_prevLineU.resize(VIDEO_WIDTH); m_prevLineV.resize(VIDEO_WIDTH);
        for (int x = 0; x < VIDEO_WIDTH; x++) {
            int idx = std::min((int)(x * (float)as / VIDEO_WIDTH), (int)m_lineBufferU.size()-1);
            if (idx >= 0) { m_prevLineU[x] = m_lineBufferU[idx]; m_prevLineV[x] = m_lineBufferV[idx]; }
        }
    }
}

void PALDecoder::buildFrame(){
    m_frameCount++;
    if(m_frameCallback) m_frameCallback(m_frameBuffer.data(),VIDEO_WIDTH,VIDEO_HEIGHT);
}

void PALDecoder::yuv2rgb(float y,float u,float v,uint8_t&r,uint8_t&g,uint8_t&b){
    r=(uint8_t)clip((y+1.14f*v)*255,0,255);
    g=(uint8_t)clip((y-0.396f*u-0.581f*v)*255,0,255);
    b=(uint8_t)clip((y+2.029f*u)*255,0,255);
}
