#ifndef PALDECODER_H
#define PALDECODER_H

#include <QObject>
#include <QImage>
#include <vector>
#include <QMutex>
#include <complex>
#include <deque>
#include <cstdint>
#include <cmath>

class PALDecoder : public QObject
{
    Q_OBJECT
public:
    explicit PALDecoder(QObject *parent = nullptr);
    ~PALDecoder();

    void processSamples(const int8_t* data, size_t len);
    void processSamples(const std::vector<std::complex<float>>& samples);
    QImage getCurrentFrame() const;

    void setTuneFrequency(uint64_t freqHz);
    void setSampleRate(int sampleRate);

    void setVideoGain(float gain) { m_videoGain = gain; }
    void setVideoOffset(float offset) { m_videoOffset = offset; }
    void setVideoInvert(bool invert) { m_videoInvert = invert; }
    void setSyncThreshold(float threshold) { m_syncLevel = threshold; }
    void setColorMode(bool color) { m_colorMode = color; }
    void setChromaGain(float gain) { m_chromaGain = gain; }
    // Auto sync slicer: threshold follows (sync tip + back porch) / 2 while locked
    void setAutoSyncThreshold(bool on) { m_autoSync = on; }
    // Frame-to-frame noise reduction (motion adaptive)
    void setTemporalDenoise(bool on) { m_temporalDenoise = on; }
    bool getAutoSyncThreshold() const { return m_autoSync; }
    // Automatic frequency control: trims the NCO so the video carrier sits at DC
    void setAfcEnabled(bool on) { m_afcEnabled = on; if (!on) { m_afcTrimHz = 0.0f; applyNcoIncrement(); } }
    float getAfcTrimHz() const { return m_afcTrimHz; }
    // Synchronous (carrier-phase) detection instead of plain envelope detection
    void setSyncDemod(bool on) { m_syncDemod = on; }
    // Vestigial-sideband compensation (halves the doubled low-frequency region)
    void setVsbCompensation(bool on) { m_vsbComp = on; }

    float getVideoGain() const { return m_videoGain; }
    float getVideoOffset() const { return m_videoOffset; }
    bool getVideoInvert() const { return m_videoInvert; }
    float getSyncThreshold() const { return m_syncLevel; }
    bool getColorMode() const { return m_colorMode; }
    float getChromaGain() const { return m_chromaGain; }

signals:
    void frameReady(const QImage& frame);
    void syncStatsUpdated(float syncRate, float peakLevel, float minLevel);

private:
    // ========== PAL-B/G Standard (fixed) ==========
    static constexpr int NB_LINES = 625;
    static constexpr float FPS = 25.0f;
    static constexpr float LINE_DURATION_US = 64.0f;

    // Timing fractions (ITU-R BT.1700)
    static constexpr float SYNC_PULSE_FRAC    = 4.7f / 64.0f;
    static constexpr float BLANKING_FRAC      = 12.0f / 64.0f;
    static constexpr float ACTIVE_VIDEO_START_FRAC = 12.05f / 64.0f;  // sync + back porch + guard
    static constexpr float HSYNC_CROP_FRAC    = 0.085f;
    static constexpr float FIELD_DETECT_START = 2.35f / 64.0f;
    static constexpr float FIELD_DETECT_END   = 27.3f / 64.0f;
    static constexpr float HALF_LINE          = 32.0f / 64.0f;

    static constexpr int VSYNC_LINES = 3;
    static constexpr int FIRST_VISIBLE_LINE = 23;

    static constexpr int VIDEO_WIDTH = 720;
    static constexpr int VIDEO_HEIGHT = 576;

    static constexpr float COLOR_CARRIER_FREQ = 4433618.75f;

    mutable QMutex m_processMutex;

    // ========== Dynamic Sample Rate ==========
    int m_sampleRate;           // input sample rate (8-20 MHz)
    int m_decimFactor;          // decimation factor (1, 2, or 3)
    float m_decimatedRate;      // m_sampleRate / m_decimFactor
    float m_chromaBandwidth;    // adjusted per rate

    // ========== NCO ==========
    double m_ncoPhase;
    double m_ncoPhaseIncrement;
    std::complex<double> m_ncoOsc{1.0, 0.0};   // rotating oscillator (no trig per sample)
    std::complex<double> m_ncoStep{1.0, 0.0};
    int m_ncoRenormCount = 0;
    void applyNcoIncrement();   // rebuilds m_ncoStep from carrier offset + AFC trim

    // ========== Carrier tracker (AFC + synchronous detection) ==========
    // A ~200 kHz complex one-pole around DC isolates the video carrier
    // (audio carrier / video sidebands are rejected), giving a clean phase
    // reference and a frequency-error estimate.
    std::complex<float> m_carrLP{0.0f, 0.0f};
    std::complex<float> m_carrPrev{0.0f, 0.0f};
    float m_carrLPCoeff = 0.1f;
    std::complex<double> m_afcAcc{0.0, 0.0};
    double m_afcSigPow = 0.0, m_afcTotPow = 0.0;
    int    m_afcCount = 0;
    int    m_afcWindow = 625000;
    bool   m_afcEnabled = true;
    float  m_afcTrimHz = 0.0f;
    bool   m_syncDemod = true;
    void   runAfc();

    // VSB compensation one-pole (post detection)
    bool  m_vsbComp = true;
    float m_vsbLPState = 0.0f;
    float m_vsbLPCoeff = 0.2f;
    float m_videoCarrierOffsetHz;
    uint64_t m_tuneFrequency;
    void updateNCO();

    // ========== Sample Counter Line Timing (at m_sampleRate) ==========
    int m_samplesPerLine;
    float m_samplesPerLineFrac;
    int m_sampleOffset;
    float m_sampleOffsetFrac;
    int m_sampleOffsetDetected;
    float m_hSyncShift;
    int m_hSyncErrorCount;
    float m_prevSample;

    int m_numberSamplesPerHTop;
    int m_numberSamplesActiveStart;   // sample offset where active video begins
    int m_numberSamplesPerLineSignals;
    int m_numberSamplesHSyncCrop;

    // Sync pulse width validation: count how many samples stay below threshold
    // to distinguish real sync pulses (~4.7 us) from video content dips
    int m_syncPulseCounter;          // samples below threshold in current candidate pulse
    int m_syncPulseMinWidth;         // minimum pulse width to accept (samples) ~2 us
    int m_syncPulseMaxWidth;         // maximum pulse width to accept (samples) ~6 us
    float m_syncPulseEntryFrac;      // fractional sample at zero-crossing entry
    int m_syncPulseEntryOffset;      // m_sampleOffset when pulse started
    float m_syncPulseEntryOffsetFrac; // m_sampleOffsetFrac when pulse started
    bool m_syncPulseActive;          // currently tracking a candidate pulse

    // ========== VSync ==========
    int m_lineIndex;
    int m_fieldIndex;
    int m_fieldDetectStartPos;
    int m_fieldDetectEndPos;
    int m_vSyncDetectStartPos;
    int m_vSyncDetectEndPos;
    int m_fieldDetectSampleCount;
    int m_vSyncDetectSampleCount;
    int m_vSyncDetectThreshold;
    int m_fieldDetectThreshold1;
    int m_fieldDetectThreshold2;

    // ========== Filters ==========
    // FIR delay lines are double-length circular buffers: buf[pos+i] is the
    // i-th most recent sample (i=0 newest), so the MAC loop is contiguous.
    std::vector<float> m_videoFilterTaps;
    std::vector<std::complex<float>> m_videoFilterBuf;
    int m_videoFilterPos = 0;
    std::vector<float> m_lumaFilterTaps;
    std::vector<float> m_lumaFilterBuf;
    int m_lumaFilterPos = 0;
    std::vector<float> m_chromaFilterTaps;      // 4.43 MHz band-pass (pre-demod)
    std::vector<float> m_chromaBandBuf;
    int m_chromaBandPos = 0;
    float m_chromaLPUState;                     // post-demod one-pole LPF (U)
    float m_chromaLPVState;                     // post-demod one-pole LPF (V)
    float m_chromaLPCoeff;

    float m_dcBlockerX1;
    float m_dcBlockerY1;
    float m_dcBlockAlpha;         // computed per sample rate (~30 Hz cutoff)
    int m_resampleCounter;

    // Sync path one-pole LPF (noise reduction before threshold detection)
    float m_syncLPState;
    float m_syncLPCoeff;

    // Sync flywheel lock state
    int  m_syncLockCount;         // consecutive good syncs
    bool m_syncLocked;            // true after sustained good syncs

    // Audio carrier notch filter (IIR biquad) - removes 5.5 MHz beat after AM demod
    float m_notchB0, m_notchB1, m_notchB2, m_notchA1, m_notchA2;
    float m_notchX1, m_notchX2, m_notchY1, m_notchY2;

    // Chroma subcarrier notch (4.43 MHz) - removes subcarrier from luma to prevent color stripes
    // Cascaded 2-stage biquad for deeper notch (~40 dB instead of ~20 dB)
    float m_chromaNotchB0, m_chromaNotchB1, m_chromaNotchB2, m_chromaNotchA1, m_chromaNotchA2;
    float m_chromaNotchX1, m_chromaNotchX2, m_chromaNotchY1, m_chromaNotchY2;
    // Second stage (same coefficients, independent state)
    float m_chromaNotch2X1, m_chromaNotch2X2, m_chromaNotch2Y1, m_chromaNotch2Y2;

    // Chroma accumulators (full-rate chroma demod, averaged over decimation period)
    float m_chromaUAccum;
    float m_chromaVAccum;

    // ========== AGC ==========
    float m_ampMin;
    float m_ampMax;
    float m_ampDelta;
    float m_effMin;
    float m_effMax;
    int m_amSampleIndex;

    // ========== Frame Buffer ==========
    std::vector<float> m_lineBuffer;
    std::vector<float> m_lineBufferU;
    std::vector<float> m_lineBufferV;
    std::vector<uint8_t> m_frameBuffer;

    // ========== User Controls ==========
    float m_videoGain;
    float m_videoOffset;
    bool m_videoInvert;
    float m_syncLevel;
    bool m_colorMode;
    float m_chromaGain;
    bool m_hSyncEnabled;
    bool m_vSyncEnabled;

    // ========== Statistics ==========
    uint64_t m_totalSamples;
    uint64_t m_frameCount;
    uint64_t m_linesProcessed;
    uint64_t m_syncDetected;

    // Flywheel-based sync quality:
    // Accumulate abs(hSyncShift) for lines where sync WAS found,
    // and count lines where sync was NOT found (free-running).
    // syncQuality = weighted metric: low error + high detection = good.
    uint64_t m_syncQualityWindow;      // lines in current measurement window
    uint64_t m_syncFoundInWindow;      // lines where zero-crossing was detected
    double   m_syncErrorAccum;         // sum of |hSyncShift| in window
    float    m_lastSyncQuality;        // 0..100 computed at each reporting interval

    // ========== Color ==========
    bool m_vPhaseAlternate;
    // Subcarrier NCO: exact phase accumulator + one-cycle sin/cos LUT.
    // (The old scheme indexed a ~100-cycle table whose length truncation
    // caused a ~120 deg phase JUMP at every wrap - several times per line -
    // which made coherent chroma demodulation impossible.)
    std::vector<float> m_colorCarrierSin;   // one full cycle, SC_LUT_SIZE entries
    std::vector<float> m_colorCarrierCos;
    double m_scPhase;                        // accumulated phase [0, 2pi)
    double m_scPhaseInc;                     // 2*pi*fsc/fs
    static constexpr int SC_LUT_SIZE = 4096;
    std::vector<float> m_prevLineU;
    std::vector<float> m_prevLineV;
    std::vector<float> m_curLineU;   // reused per-line scratch (swapped with prev)
    std::vector<float> m_curLineV;

    // ========== Auto sync slicer ==========
    bool  m_autoSync = false;
    int   m_tipStart = 0, m_tipEnd = 0;         // sync tip window (samples from line start)
    int   m_porchStart = 0, m_porchEnd = 0;     // back porch (black level) window
    double m_tipSum = 0.0, m_porchSum = 0.0;
    int   m_tipCount = 0, m_porchCount = 0;

    // ========== Temporal denoise ==========
    bool m_temporalDenoise = true;
    std::vector<uint8_t> m_denoiseBuf;

    // ========== Colour Burst PLL ==========
    // Back porch burst window (samples at full rate)
    int m_burstStartSample;       // start of burst window (~5.6 us from line start)
    int m_burstEndSample;         // end of burst window (~7.85 us)

    // Burst correlation accumulators (per line)
    float m_burstCorrI;           // sum(sample * cos(2pi*fsc*t))
    float m_burstCorrQ;           // sum(sample * sin(2pi*fsc*t))
    float m_burstDCAccum;         // sum(sample) for DC removal
    float m_burstCosAccum;        // sum(cos(2pi*fsc*t)) for DC removal
    float m_burstSinAccum;        // sum(sin(2pi*fsc*t)) for DC removal
    int   m_burstSampleCount;     // samples accumulated in burst window

    // Extracted burst phase & amplitude
    float m_burstAmplitude;       // measured burst amplitude (for chroma AGC)
    bool  m_burstValid;           // true if burst was detected this line

    // Phase-locked reference (derived from burst)
    float m_chromaRefPhase;       // reference phase for chroma demod this line

    // Swinging-burst mean-axis tracking:
    // PAL burst alternates +/-45 deg around a mean axis (= U axis + 180 deg).
    // Vector-averaging two consecutive line bursts removes the alternation,
    // giving a stable U-axis reference and per-line V-switch detection.
    bool  m_burstMeanInit;        // mean axis has been initialized
    float m_burstMeanPhase;       // smoothed mean burst axis angle
    float m_prevBurstAngle;       // previous line's measured burst angle
    bool  m_prevBurstValid;       // previous line had a valid burst
    bool  m_burstSeenThisLine;    // burst extracted on current line
    int   m_burstMissCount;       // consecutive lines without valid burst
    bool  m_chromaMute;           // mute chroma when burst absent (B/W signal)

    // Cached sin/cos of reference phase (computed once per line after burst extraction)
    float m_chromaCosRef;         // cos(m_chromaRefPhase)
    float m_chromaSinRef;         // sin(m_chromaRefPhase)

    // Burst amplitude AGC
    float m_burstAmpSmoothed;     // smoothed burst amplitude for chroma scaling

    // ========== Methods ==========
    void applyStandard();
    void initFilters();
    void initNotchFilter();
    void initBurstPLL();
    void rebuildColorLUT();
    std::vector<float> designLowPassFIR(float cutoff, float sampleRate, int numTaps);
    std::vector<float> designBandPassFIR(float centerFreq, float bandwidth, float sampleRate, int numTaps);
    std::complex<float> applyVideoFilter(const std::complex<float>& sample);
    float applyLumaFilter(float sample);
    float applyChromaBandFilter(float sample);
    float dcBlock(float sample);
    float normalizeAndAGC(float sample);
    void processSample(float sample);
    void processEndOfLine();
    void renderLine();
    void buildFrame();
    float clipValue(float value, float min, float max);
    void yuv2rgb(float y, float u, float v, uint8_t& r, uint8_t& g, uint8_t& b);
    void accumulateBurst(float sample, float cosVal, float sinVal);
    void extractBurstPhase();
};

#endif
