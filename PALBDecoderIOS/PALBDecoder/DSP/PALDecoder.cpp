#include "PALDecoder.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    , m_syncLevel(0), m_chromaGain(0.75f), m_hSyncEnabled(true), m_vSyncEnabled(true)
    , m_totalSamples(0), m_frameCount(0), m_linesProcessed(0), m_syncDetected(0)
    , m_syncQualityWindow(0), m_syncFoundInWindow(0), m_syncErrorAccum(0), m_lastSyncQuality(0)
    , m_vPhaseAlternate(false), m_colorCarrierIndex(0)
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
    applyStandard(); initFilters(); rebuildColorLUT(); updateNCO();
    m_resampleCounter = 0; m_sampleOffset = 0; m_sampleOffsetFrac = 0;
    m_hSyncShift = 0; m_hSyncErrorCount = 0; m_lineIndex = 0; m_fieldIndex = 0; m_colorCarrierIndex = 0;
    m_ampMin = -1; m_ampMax = 1; m_ampDelta = 2; m_effMin = 20; m_effMax = -20; m_amSampleIndex = 0;
    m_ncoAccum = 0;
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
    float lc = std::min(3.0e6f, m_decimatedRate * 0.35f);
    m_lumaFilterTaps = designLowPassFIR(lc, m_decimatedRate, 17);
    m_lumaFir.setLen(17);
    if (COLOR_CARRIER_FREQ < r / 2.0f) {
        m_chromaFilterTaps = designBandPassFIR(COLOR_CARRIER_FREQ, m_chromaBandwidth, r, 25);
        m_chromaFirU.setLen(25); m_chromaFirV.setLen(25);
    } else { m_chromaFilterTaps.clear(); m_chromaFirU.setLen(1); m_chromaFirV.setLen(1); }
}

void PALDecoder::rebuildColorLUT() {
    float r = (float)m_sampleRate;
    if (COLOR_CARRIER_FREQ >= r/2) { m_colorCarrierSin.clear(); m_colorCarrierCos.clear(); return; }
    int n = (int)(r / COLOR_CARRIER_FREQ * 100 + 0.5f); if (n < 200) n = 200;
    m_colorCarrierSin.resize(n); m_colorCarrierCos.resize(n);
    for (int i = 0; i < n; i++) {
        double p = 2.0*M_PI*COLOR_CARRIER_FREQ*i/(double)r;
        m_colorCarrierSin[i] = (float)sin(p); m_colorCarrierCos[i] = (float)cos(p);
    }
}

void PALDecoder::setTuneFrequency(uint64_t f) { m_tuneFrequency = f; updateNCO(); }

void PALDecoder::updateNCO() {
    double t = m_tuneFrequency / 1e6, vc;
    if (t >= 470 && t <= 862) { int ch = (int)floor((t-470-0.001)/8); vc = 470+std::max(ch,0)*8.0+1.25; }
    else if (t >= 174 && t <= 230) { int ch = (int)floor((t-174-0.001)/8); vc = 174+std::max(ch,0)*8.0+1.25; }
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
    std::vector<float> t(n); float fc=cf/sr, b=bw/sr/2; int M=n-1;
    for (int i=0;i<n;i++) {
        float mm=i-M/2.0f;
        float h=(mm==0)?2*b:(sinf(2*M_PI*(fc+b)*mm)-sinf(2*M_PI*(fc-b)*mm))/(M_PI*mm);
        t[i]=h*(0.42f-0.5f*cosf(2*M_PI*i/M)+0.08f*cosf(4*M_PI*i/M))*2*cosf(2*M_PI*fc*mm);
    }
    float s=0; for(int i=0;i<n;i++){float mm=i-(n-1)/2.0f;s+=t[i]*cosf(2*M_PI*fc*mm);}
    if(fabsf(s)>1e-6f) for(auto&v:t) v/=fabsf(s);
    return t;
}

// Not used in hot loop anymore - kept for compatibility
float PALDecoder::normalizeAndAGC(float s) {
    if(s<m_effMin)m_effMin=s; if(s>m_effMax)m_effMax=s;
    if(++m_amSampleIndex >= m_samplesPerLine*NB_LINES*2) {
        m_ampMax=m_effMax; float r=m_effMax-m_effMin;
        m_ampMin=m_effMin-r*0.1f; m_ampDelta=m_ampMax-m_ampMin;
        if(m_ampDelta<=0)m_ampDelta=1;
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
    const bool doColor = m_colorMode && !m_colorCarrierSin.empty();
    const int cSize = doColor ? (int)m_colorCarrierSin.size() : 1;
    const float invD = 1.0f / m_decimFactor;
    const float* lTaps = m_lumaFilterTaps.data();
    const size_t half = len / 2;

    // Cache AGC values locally to avoid member access in tight loop
    float ampMin = m_ampMin, ampDelta = m_ampDelta;
    float effMin = m_effMin, effMax = m_effMax;
    int amIdx = m_amSampleIndex;
    const int agcPeriod = m_samplesPerLine * NB_LINES * 2;
    float ampMax = m_ampMax;

    // Cache sync values locally
    float prevSample = m_prevSample;
    const float syncLevel = m_syncLevel;
    const int spl = m_samplesPerLine;
    const int hTop = m_numberSamplesPerHTop;
    const int hSync = m_numberSamplesPerHSync;

    float accumI = 0, accumQ = 0;

    for (size_t i = 0; i < half; i++) {
        float sI = data[i*2] * (1.0f/128.0f);
        float sQ = data[i*2+1] * (1.0f/128.0f);

        // NCO freq shift
        uint32_t idx = m_ncoAccum >> (32 - NCO_LUT_BITS);
        float shI = sI * m_ncoCos[idx] - sQ * m_ncoSin[idx];
        float shQ = sI * m_ncoSin[idx] + sQ * m_ncoCos[idx];
        m_ncoAccum += m_ncoStep;

        // Fast magnitude (no FIR, no dcBlock, no sqrtf)
        float ai = fabsf(shI), aq = fabsf(shQ);
        float mag = (ai > aq) ? ai + 0.4f * aq : aq + 0.4f * ai;

        // Inline AGC
        if (mag < effMin) effMin = mag;
        if (mag > effMax) effMax = mag;
        if (++amIdx >= agcPeriod) {
            ampMax = effMax; float r = effMax - effMin;
            ampMin = effMin - r * 0.1f; ampDelta = ampMax - ampMin;
            if (ampDelta <= 0) ampDelta = 1;
            effMin = 20; effMax = -20; amIdx = 0;
        }
        float norm = (mag - ampMin);
        if (ampDelta > 0) norm *= (1.0f / ampDelta);
        if (norm > 1) norm = 1; else if (norm < 0) norm = 0;

        // Inline sync detection (no function call overhead)
        if (prevSample >= syncLevel && norm < syncLevel &&
            m_sampleOffsetDetected > spl - hTop) {
            float fr = (norm - syncLevel) / (prevSample - norm);
            float hs = -m_sampleOffset - m_sampleOffsetFrac - fr;
            if (hs > spl/2) hs -= spl;
            else if (hs < -spl/2) hs += spl;
            if (fabsf(hs) > hTop) {
                m_hSyncErrorCount++;
                if (m_hSyncErrorCount >= 4) { m_hSyncShift = hs; m_hSyncErrorCount = 0; }
            } else {
                m_hSyncShift = hs * 0.2f; m_hSyncErrorCount = 0;
            }
            m_syncDetected++; m_syncFoundInWindow++; m_sampleOffsetDetected = 0;
            m_syncErrorAccum += fabsf(hs);
        } else {
            m_sampleOffsetDetected++;
        }
        prevSample = norm;

        m_sampleOffset++;
        // VSync detection
        if (m_sampleOffset > m_fieldDetectStartPos && m_sampleOffset < m_fieldDetectEndPos)
            m_fieldDetectSampleCount += (norm < syncLevel) ? 1 : 0;
        if (m_sampleOffset > m_vSyncDetectStartPos && m_sampleOffset < m_vSyncDetectEndPos)
            m_vSyncDetectSampleCount += (norm < syncLevel) ? 1 : 0;

        // End of line
        if (m_sampleOffset >= spl) {
            float sof = m_hSyncShift + m_sampleOffsetFrac - m_samplesPerLineFrac;
            m_sampleOffset = (int)sof; m_sampleOffsetFrac = sof - m_sampleOffset; m_hSyncShift = 0;
            m_lineIndex++; m_linesProcessed++; m_syncQualityWindow++;
            processEndOfLine();

            // Stats check per-line (not per-sample - was modulo every sample!)
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

        // Accumulate for decimation
        accumI += shI;
        accumQ += shQ;

        // Chroma at full rate (only if color mode)
        if (doColor) {
            float cs = m_colorCarrierSin[m_colorCarrierIndex];
            float cc = m_colorCarrierCos[m_colorCarrierIndex];
            m_chromaFirU.push(norm * cs); m_chromaFirV.push(norm * cc);
            m_chromaUAccum += m_chromaFirU.apply(m_chromaFilterTaps.data());
            m_chromaVAccum += m_chromaFirV.apply(m_chromaFilterTaps.data());
            if (++m_colorCarrierIndex >= cSize) m_colorCarrierIndex = 0;
        }

        // Decimate
        if (++m_resampleCounter < m_decimFactor) continue;
        m_resampleCounter = 0;

        float avgI = accumI * invD, avgQ = accumQ * invD;
        accumI = 0; accumQ = 0;
        float dMag = fastMag(avgI, avgQ);
        float dNorm = (dMag - ampMin);
        if (ampDelta > 0) dNorm *= (1.0f / ampDelta);
        if (dNorm > 1) dNorm = 1; else if (dNorm < 0) dNorm = 0;

        m_lumaFir.push(dNorm);
        float luma = m_lumaFir.apply(lTaps);
        float u = 0, v = 0;
        if (doColor) {
            u = m_chromaUAccum * invD * 2.5f;
            v = m_chromaVAccum * invD * 2.5f * (m_vPhaseAlternate ? -1 : 1);
            m_chromaUAccum = 0; m_chromaVAccum = 0;
        }
        if (m_sampleOffset > hSync) {
            m_lineBuffer.push_back(luma);
            m_lineBufferU.push_back(u);
            m_lineBufferV.push_back(v);
        }
    }

    // Write back cached AGC state
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

// Not used in hot loop - kept for external callers
void PALDecoder::processSample(float s) {
    if(m_hSyncEnabled) {
        if(m_prevSample>=m_syncLevel && s<m_syncLevel && m_sampleOffsetDetected>m_samplesPerLine-m_numberSamplesPerHTop) {
            float fr=(s-m_syncLevel)/(m_prevSample-s);
            float hs=-m_sampleOffset-m_sampleOffsetFrac-fr;
            if(hs>m_samplesPerLine/2)hs-=m_samplesPerLine;
            else if(hs<-m_samplesPerLine/2)hs+=m_samplesPerLine;
            if(fabsf(hs)>m_numberSamplesPerHTop){m_hSyncErrorCount++;if(m_hSyncErrorCount>=4){m_hSyncShift=hs;m_hSyncErrorCount=0;}m_syncErrorAccum+=fabsf(hs);}
            else{m_hSyncShift=hs*0.2f;m_hSyncErrorCount=0;m_syncErrorAccum+=fabsf(hs);}
            m_syncDetected++;m_syncFoundInWindow++;m_sampleOffsetDetected=0;
        } else m_sampleOffsetDetected++;
    }
    m_sampleOffset++;
    if(m_vSyncEnabled){
        if(m_sampleOffset>m_fieldDetectStartPos&&m_sampleOffset<m_fieldDetectEndPos)m_fieldDetectSampleCount+=(s<m_syncLevel)?1:0;
        if(m_sampleOffset>m_vSyncDetectStartPos&&m_sampleOffset<m_vSyncDetectEndPos)m_vSyncDetectSampleCount+=(s<m_syncLevel)?1:0;
    }
    if(m_sampleOffset>=m_samplesPerLine){
        float sof=m_hSyncShift+m_sampleOffsetFrac-m_samplesPerLineFrac;
        m_sampleOffset=(int)sof;m_sampleOffsetFrac=sof-m_sampleOffset;m_hSyncShift=0;
        m_lineIndex++;m_linesProcessed++;m_syncQualityWindow++;
        processEndOfLine();
    }
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
    m_vPhaseAlternate=!m_vPhaseAlternate;m_colorCarrierIndex=0;
}

void PALDecoder::renderLine(){
    int row=(m_lineIndex-FIRST_VISIBLE_LINE)*2-m_fieldIndex;
    if(row<0||row>=VIDEO_HEIGHT)return;
    int as=(int)m_lineBuffer.size(); if(as<10)return;
    uint8_t*rp=m_frameBuffer.data()+row*VIDEO_WIDTH*4;
    float sc=(float)as/VIDEO_WIDTH;
    for(int x=0;x<VIDEO_WIDTH;x++){
        float sx=x*sc; int idx=(int)sx; float fr=sx-idx;
        float Y=m_lineBuffer[idx]; if(idx+1<as) Y+=(m_lineBuffer[idx+1]-Y)*fr;
        Y=Y*m_videoGain+m_videoOffset; Y=clip(Y,0,1);
        float U=0,V=0;
        if(m_colorMode&&idx<(int)m_lineBufferU.size()){
            int i2=std::min(idx+1,(int)m_lineBufferU.size()-1);
            U=m_lineBufferU[idx]+(m_lineBufferU[i2]-m_lineBufferU[idx])*fr;
            V=m_lineBufferV[idx]+(m_lineBufferV[i2]-m_lineBufferV[idx])*fr;
            if(!m_prevLineU.empty()&&x<(int)m_prevLineU.size()){U=(U+m_prevLineU[x])*0.5f;V=(V-m_prevLineV[x])*0.5f;}
            U*=m_chromaGain;V*=m_chromaGain;
        }
        uint8_t r,g,b; yuv2rgb(Y,U,V,r,g,b);
        if(m_videoInvert){r=255-r;g=255-g;b=255-b;}
        int o=x*4; rp[o]=r;rp[o+1]=g;rp[o+2]=b;rp[o+3]=255;
    }
    if(m_colorMode){m_prevLineU.resize(VIDEO_WIDTH);m_prevLineV.resize(VIDEO_WIDTH);
        for(int x=0;x<VIDEO_WIDTH;x++){int idx=std::min((int)(x*sc),(int)m_lineBufferU.size()-1);
            if(idx>=0){m_prevLineU[x]=m_lineBufferU[idx];m_prevLineV[x]=m_lineBufferV[idx];}}}
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
