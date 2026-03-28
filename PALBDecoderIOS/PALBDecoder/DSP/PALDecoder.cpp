#include "PALDecoder.h"
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PALDecoder::PALDecoder()
    : m_sampleRate(16000000), m_decimFactor(2), m_decimatedRate(8000000.0f), m_chromaBandwidth(1.2e6f)
    , m_ncoPhase(0.0), m_ncoPhaseIncrement(0.0), m_videoCarrierOffsetHz(0.0f), m_tuneFrequency(638000000ULL)
    , m_samplesPerLine(1024), m_samplesPerLineFrac(0.0f)
    , m_sampleOffset(0), m_sampleOffsetFrac(0.0f), m_sampleOffsetDetected(0)
    , m_hSyncShift(0.0f), m_hSyncErrorCount(0), m_prevSample(0.0f)
    , m_numberSamplesPerHTop(0), m_numberSamplesPerHSync(0), m_numberSamplesPerLineSignals(0)
    , m_lineIndex(0), m_fieldIndex(0)
    , m_fieldDetectStartPos(0), m_fieldDetectEndPos(0), m_vSyncDetectStartPos(0), m_vSyncDetectEndPos(0)
    , m_fieldDetectSampleCount(0), m_vSyncDetectSampleCount(0)
    , m_vSyncDetectThreshold(0), m_fieldDetectThreshold1(0), m_fieldDetectThreshold2(0)
    , m_dcBlockerX1(0.0f), m_dcBlockerY1(0.0f), m_resampleCounter(0)
    , m_notchB0(1), m_notchB1(0), m_notchB2(0), m_notchA1(0), m_notchA2(0)
    , m_notchX1(0), m_notchX2(0), m_notchY1(0), m_notchY2(0)
    , m_chromaNotchB0(1), m_chromaNotchB1(0), m_chromaNotchB2(0), m_chromaNotchA1(0), m_chromaNotchA2(0)
    , m_chromaNotchX1(0), m_chromaNotchX2(0), m_chromaNotchY1(0), m_chromaNotchY2(0)
    , m_chromaUAccum(0), m_chromaVAccum(0)
    , m_ampMin(-1), m_ampMax(1), m_ampDelta(2), m_effMin(20), m_effMax(-20), m_amSampleIndex(0)
    , m_videoGain(1.5f), m_videoOffset(0.0f), m_videoInvert(true), m_colorMode(false)
    , m_syncLevel(0.05f), m_chromaGain(0.75f), m_hSyncEnabled(true), m_vSyncEnabled(true)
    , m_totalSamples(0), m_frameCount(0), m_linesProcessed(0), m_syncDetected(0)
    , m_syncQualityWindow(0), m_syncFoundInWindow(0), m_syncErrorAccum(0), m_lastSyncQuality(0)
    , m_vPhaseAlternate(false), m_colorCarrierIndex(0)
{
    m_frameBuffer.resize(VIDEO_WIDTH * VIDEO_HEIGHT * 4, 0);
    m_lineBuffer.reserve(2048); m_lineBufferU.reserve(2048); m_lineBufferV.reserve(2048);
    setSampleRate(16000000);
    updateNCO();
}

PALDecoder::~PALDecoder() {}

void PALDecoder::setSampleRate(int sr) {
    std::lock_guard<std::mutex> lock(m_processMutex);
    m_sampleRate = sr;
    m_decimFactor = (m_sampleRate <= 12500000) ? 1 : 2;
    m_decimatedRate = (float)m_sampleRate / m_decimFactor;
    m_chromaBandwidth = (m_sampleRate >= 16000000) ? 1.5e6f : 1.2e6f;
    applyStandard(); initFilters(); initNotchFilter(); rebuildColorLUT();
    m_resampleCounter = 0; m_sampleOffset = 0; m_sampleOffsetFrac = 0;
    m_hSyncShift = 0; m_hSyncErrorCount = 0; m_lineIndex = 0; m_fieldIndex = 0; m_colorCarrierIndex = 0;
    m_ampMin = -1; m_ampMax = 1; m_ampDelta = 2; m_effMin = 20; m_effMax = -20; m_amSampleIndex = 0;
    m_notchX1 = m_notchX2 = m_notchY1 = m_notchY2 = 0;
    m_chromaNotchX1 = m_chromaNotchX2 = m_chromaNotchY1 = m_chromaNotchY2 = 0;
    m_videoFilterDelay.clear(); m_lumaFilterDelay.clear();
    m_chromaUFilterDelay.clear(); m_chromaVFilterDelay.clear();
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
    m_fieldDetectThreshold1 = (int)(dtl * 0.75f); m_fieldDetectThreshold2 = (int)(dtl * 0.25f);
    m_vSyncDetectThreshold = (int)(dtl * 0.5f);
}

void PALDecoder::initFilters() {
    float r = (float)m_sampleRate;
    float vc = std::min(5.0e6f, r * 0.40f);
    int vt = std::max(17, (int)(r/vc)*4+1); if(vt>65)vt=65; if(vt%2==0)vt++;
    m_videoFilterTaps = designLowPassFIR(vc, r, vt);
    float lc = std::min(5.0e6f, m_decimatedRate * 0.45f);
    int lt = (m_decimatedRate >= 10e6f) ? 33 : 21;
    m_lumaFilterTaps = designLowPassFIR(lc, m_decimatedRate, lt);
    if (COLOR_CARRIER_FREQ < r/2) m_chromaFilterTaps = designBandPassFIR(COLOR_CARRIER_FREQ, m_chromaBandwidth, r, 31);
    else m_chromaFilterTaps.clear();
}

static void designNotch(float freq, float bw, float rate, float& b0, float& b1, float& b2, float& a1, float& a2) {
    if (freq >= rate/2) { freq = rate - freq; if(freq<0)freq=-freq; }
    float w0 = 2*M_PI*freq/rate, bwr = 2*M_PI*bw/rate;
    float R = 1-bwr/2; if(R<0.8f)R=0.8f; if(R>0.999f)R=0.999f;
    float c = cosf(w0);
    b0=1; b1=-2*c; b2=1; a1=-2*R*c; a2=R*R;
    float dc = (b0+b1+b2)/(1+a1+a2);
    if(fabsf(dc)>0.01f){b0/=dc;b1/=dc;b2/=dc;}
}

void PALDecoder::initNotchFilter() {
    float r = (float)m_sampleRate;
    designNotch(5.5e6f, 300e3f, r, m_notchB0, m_notchB1, m_notchB2, m_notchA1, m_notchA2);
    designNotch(COLOR_CARRIER_FREQ, 600e3f, r, m_chromaNotchB0, m_chromaNotchB1, m_chromaNotchB2, m_chromaNotchA1, m_chromaNotchA2);
    m_notchX1=m_notchX2=m_notchY1=m_notchY2=0;
    m_chromaNotchX1=m_chromaNotchX2=m_chromaNotchY1=m_chromaNotchY2=0;
}

void PALDecoder::rebuildColorLUT() {
    float r = (float)m_sampleRate;
    if (COLOR_CARRIER_FREQ >= r/2) { m_colorCarrierSin.clear(); m_colorCarrierCos.clear(); return; }
    int n = (int)(r/COLOR_CARRIER_FREQ*100+0.5f); if(n<200)n=200;
    m_colorCarrierSin.resize(n); m_colorCarrierCos.resize(n);
    for (int i=0;i<n;i++) { double p=2*M_PI*COLOR_CARRIER_FREQ*i/(double)r; m_colorCarrierSin[i]=(float)sin(p); m_colorCarrierCos[i]=(float)cos(p); }
}

void PALDecoder::setTuneFrequency(uint64_t f) { m_tuneFrequency = f; updateNCO(); }

void PALDecoder::updateNCO() {
    double t = m_tuneFrequency/1e6, vc;
    if (t>=470&&t<=862) { int n=(int)floor((t-470+0.5)/8.0); if(n<0)n=0; vc=470+n*8.0+1.25; }
    else if (t>=174&&t<=230) { int n=(int)floor((t-174+0.5)/8.0); if(n<0)n=0; vc=174+n*8.0+1.25; }
    else { vc=t; }
    m_videoCarrierOffsetHz = (float)((vc-t)*1e6);
    m_ncoPhaseIncrement = -2.0*M_PI*(double)m_videoCarrierOffsetHz/(double)m_sampleRate;
    m_ncoPhase = 0;
}

std::vector<float> PALDecoder::designLowPassFIR(float cutoff, float sr, int n) {
    std::vector<float> t(n); float fc=cutoff/sr; int M=n-1;
    for(int i=0;i<n;i++){float mm=i-M/2.0f;float h=(mm==0)?2*fc:sinf(2*M_PI*fc*mm)/(M_PI*mm);
    t[i]=h*(0.42f-0.5f*cosf(2*M_PI*i/M)+0.08f*cosf(4*M_PI*i/M));}
    float s=0;for(auto v:t)s+=v;if(s!=0)for(auto&v:t)v/=s;return t;
}

std::vector<float> PALDecoder::designBandPassFIR(float cf, float bw, float sr, int n) {
    std::vector<float> t(n);float fc=cf/sr,b=bw/sr/2;int M=n-1;
    for(int i=0;i<n;i++){float mm=i-M/2.0f;float h=(mm==0)?2*b:(sinf(2*M_PI*(fc+b)*mm)-sinf(2*M_PI*(fc-b)*mm))/(M_PI*mm);
    t[i]=h*(0.42f-0.5f*cosf(2*M_PI*i/M)+0.08f*cosf(4*M_PI*i/M))*2*cosf(2*M_PI*fc*mm);}
    float s=0;for(int i=0;i<n;i++){float mm=i-(n-1)/2.0f;s+=t[i]*cosf(2*M_PI*fc*mm);}
    if(fabsf(s)>1e-6f)for(auto&v:t)v/=fabsf(s);return t;
}

std::complex<float> PALDecoder::applyVideoFilter(const std::complex<float>& sample) {
    m_videoFilterDelay.push_front(sample);
    if(m_videoFilterDelay.size()>m_videoFilterTaps.size())m_videoFilterDelay.pop_back();
    std::complex<float> out(0,0);size_t n=std::min(m_videoFilterDelay.size(),m_videoFilterTaps.size());
    for(size_t i=0;i<n;i++)out+=m_videoFilterDelay[i]*m_videoFilterTaps[i];return out;
}

float PALDecoder::applyLumaFilter(float sample) {
    m_lumaFilterDelay.push_front(sample);
    if(m_lumaFilterDelay.size()>m_lumaFilterTaps.size())m_lumaFilterDelay.pop_back();
    float out=0;size_t n=std::min(m_lumaFilterDelay.size(),m_lumaFilterTaps.size());
    for(size_t i=0;i<n;i++)out+=m_lumaFilterDelay[i]*m_lumaFilterTaps[i];return out;
}

float PALDecoder::applyChromaFilterU(float sample) {
    m_chromaUFilterDelay.push_front(sample);
    if(m_chromaUFilterDelay.size()>m_chromaFilterTaps.size())m_chromaUFilterDelay.pop_back();
    float out=0;size_t n=std::min(m_chromaUFilterDelay.size(),m_chromaFilterTaps.size());
    for(size_t i=0;i<n;i++)out+=m_chromaUFilterDelay[i]*m_chromaFilterTaps[i];return out;
}

float PALDecoder::applyChromaFilterV(float sample) {
    m_chromaVFilterDelay.push_front(sample);
    if(m_chromaVFilterDelay.size()>m_chromaFilterTaps.size())m_chromaVFilterDelay.pop_back();
    float out=0;size_t n=std::min(m_chromaVFilterDelay.size(),m_chromaFilterTaps.size());
    for(size_t i=0;i<n;i++)out+=m_chromaVFilterDelay[i]*m_chromaFilterTaps[i];return out;
}

float PALDecoder::dcBlock(float sample) {
    float out=sample-m_dcBlockerX1+0.995f*m_dcBlockerY1;
    m_dcBlockerX1=sample;m_dcBlockerY1=out;return out;
}

float PALDecoder::normalizeAndAGC(float sample) {
    if(sample<m_effMin)m_effMin=sample;if(sample>m_effMax)m_effMax=sample;
    if(++m_amSampleIndex>=m_samplesPerLine*NB_LINES*2){
        m_ampMax=m_effMax;float range=m_effMax-m_effMin;
        m_ampMin=m_effMin-range*0.10f;m_ampDelta=m_ampMax-m_ampMin;
        if(m_ampDelta<=0)m_ampDelta=1;m_effMin=20;m_effMax=-20;m_amSampleIndex=0;
    }
    float n=(sample-m_ampMin)/m_ampDelta;return(n>1)?1:n;
}

void PALDecoder::processSamples(const int8_t* data, size_t len) {
    if(!data||len==0)return;
    std::vector<std::complex<float>> samples; samples.reserve(len/2);
    for(size_t i=0;i<len;i+=2)samples.emplace_back(data[i]/128.0f,data[i+1]/128.0f);
    processSamples(samples);
}

void PALDecoder::processSamples(const std::vector<std::complex<float>>& samples) {
    std::lock_guard<std::mutex> lock(m_processMutex);
    if(samples.empty()||samples.size()>100000000)return;

    for(const auto& sample : samples) {
        m_totalSamples++;
        if(m_totalSamples%10000000==0){
            float dr=m_syncQualityWindow>0?(float)m_syncFoundInWindow/m_syncQualityWindow:0;
            float ae=m_syncFoundInWindow>0?(float)(m_syncErrorAccum/m_syncFoundInWindow):(float)m_numberSamplesPerHTop;
            float eq=1-std::clamp(ae/(float)m_numberSamplesPerHTop,0.0f,1.0f);
            float sq=(dr*0.6f+eq*0.4f)*100;m_lastSyncQuality=sq;
            m_syncQualityWindow=0;m_syncFoundInWindow=0;m_syncErrorAccum=0;
            if(m_syncStatsCallback)m_syncStatsCallback(sq,m_ampMax,m_ampMin);
        }

        // NCO shift
        float ncoI=(float)cos(m_ncoPhase),ncoQ=(float)sin(m_ncoPhase);
        m_ncoPhase+=m_ncoPhaseIncrement;
        if(m_ncoPhase>M_PI)m_ncoPhase-=2*M_PI;else if(m_ncoPhase<-M_PI)m_ncoPhase+=2*M_PI;
        std::complex<float> shifted(sample.real()*ncoI-sample.imag()*ncoQ,sample.real()*ncoQ+sample.imag()*ncoI);

        // IQ LPF
        std::complex<float> filtered = applyVideoFilter(shifted);

        // AM envelope
        float mag = std::sqrt(filtered.real()*filtered.real()+filtered.imag()*filtered.imag());

        // Audio carrier notch (5.5 MHz)
        float notched = m_notchB0*mag+m_notchB1*m_notchX1+m_notchB2*m_notchX2-m_notchA1*m_notchY1-m_notchA2*m_notchY2;
        m_notchX2=m_notchX1;m_notchX1=mag;m_notchY2=m_notchY1;m_notchY1=notched;

        float dcBlocked = dcBlock(notched);
        float normalized = normalizeAndAGC(dcBlocked);

        // Sync
        processSample(normalized);

        // Chroma at full rate
        if(m_colorMode && !m_colorCarrierSin.empty()){
            float cs=m_colorCarrierSin[m_colorCarrierIndex],cc=m_colorCarrierCos[m_colorCarrierIndex];
            if(++m_colorCarrierIndex>=(int)m_colorCarrierSin.size())m_colorCarrierIndex=0;
            m_chromaUAccum+=applyChromaFilterU(normalized*cc);
            m_chromaVAccum+=applyChromaFilterV(normalized*cs*(m_vPhaseAlternate?-1.0f:1.0f));
        }

        // Chroma subcarrier notch (4.43 MHz) at FULL rate for luma path
        float lumaSignal;
        {
            float cn=m_chromaNotchB0*normalized+m_chromaNotchB1*m_chromaNotchX1+m_chromaNotchB2*m_chromaNotchX2
                     -m_chromaNotchA1*m_chromaNotchY1-m_chromaNotchA2*m_chromaNotchY2;
            m_chromaNotchX2=m_chromaNotchX1;m_chromaNotchX1=normalized;
            m_chromaNotchY2=m_chromaNotchY1;m_chromaNotchY1=cn;
            lumaSignal=cn;
        }

        // Decimate
        if(++m_resampleCounter<m_decimFactor)continue;
        m_resampleCounter=0;

        float luma=applyLumaFilter(lumaSignal);
        float u=0,v=0;
        if(m_colorMode&&!m_colorCarrierSin.empty()){
            float invD=1.0f/m_decimFactor;
            u=m_chromaUAccum*invD*2.5f;v=m_chromaVAccum*invD*2.5f;
            m_chromaUAccum=0;m_chromaVAccum=0;
        }
        if(m_sampleOffset>m_numberSamplesPerHSync){
            m_lineBuffer.push_back(luma);m_lineBufferU.push_back(u);m_lineBufferV.push_back(v);
        }
    }
}

void PALDecoder::processSample(float sample) {
    if(m_hSyncEnabled){
        if(m_prevSample>=m_syncLevel&&sample<m_syncLevel&&m_sampleOffsetDetected>m_samplesPerLine-m_numberSamplesPerHTop){
            float fr=(sample-m_syncLevel)/(m_prevSample-sample+1e-12f);
            float hs=-m_sampleOffset-m_sampleOffsetFrac-fr;
            if(hs>m_samplesPerLine/2)hs-=m_samplesPerLine;else if(hs<-m_samplesPerLine/2)hs+=m_samplesPerLine;
            if(fabsf(hs)>m_numberSamplesPerHTop){if(++m_hSyncErrorCount>=4){m_hSyncShift=hs;m_hSyncErrorCount=0;}}
            else{m_hSyncShift=hs*0.2f;m_hSyncErrorCount=0;}
            m_syncDetected++;m_syncFoundInWindow++;m_sampleOffsetDetected=0;m_syncErrorAccum+=fabsf(hs);
        }else{m_sampleOffsetDetected++;}
    }
    m_sampleOffset++;
    if(m_vSyncEnabled){
        if(m_sampleOffset>m_fieldDetectStartPos&&m_sampleOffset<m_fieldDetectEndPos)m_fieldDetectSampleCount+=(sample<m_syncLevel)?1:0;
        if(m_sampleOffset>m_vSyncDetectStartPos&&m_sampleOffset<m_vSyncDetectEndPos)m_vSyncDetectSampleCount+=(sample<m_syncLevel)?1:0;
    }
    if(m_sampleOffset>=m_samplesPerLine){
        float sof=m_hSyncShift+m_sampleOffsetFrac-m_samplesPerLineFrac;
        m_sampleOffset=(int)sof;m_sampleOffsetFrac=sof-m_sampleOffset;m_hSyncShift=0;
        m_lineIndex++;m_linesProcessed++;m_syncQualityWindow++;processEndOfLine();
    }
    m_prevSample=sample;
}

void PALDecoder::processEndOfLine() {
    if(m_lineIndex==VSYNC_LINES+3&&m_fieldIndex==0)buildFrame();
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

void PALDecoder::renderLine() {
    int row=(m_lineIndex-FIRST_VISIBLE_LINE)*2-m_fieldIndex;
    if(row<0||row>=VIDEO_HEIGHT)return;
    int as=(int)m_lineBuffer.size();if(as<10)return;
    uint8_t* rp=m_frameBuffer.data()+row*VIDEO_WIDTH*4;
    std::vector<float> curU(VIDEO_WIDTH,0),curV(VIDEO_WIDTH,0);
    float sc=(float)as/VIDEO_WIDTH;
    for(int x=0;x<VIDEO_WIDTH;x++){
        float sx=x*sc;int idx=(int)sx;float fr=sx-idx;
        float Y=m_lineBuffer[idx];if(idx+1<as)Y+=(m_lineBuffer[idx+1]-Y)*fr;
        Y=Y*m_videoGain+m_videoOffset;if(Y<0)Y=0;if(Y>1)Y=1;
        float U=0,V=0;
        if(m_colorMode&&idx<(int)m_lineBufferU.size()){
            int i2=std::min(idx+1,(int)m_lineBufferU.size()-1);
            U=m_lineBufferU[idx]+(m_lineBufferU[i2]-m_lineBufferU[idx])*fr;
            V=m_lineBufferV[idx]+(m_lineBufferV[i2]-m_lineBufferV[idx])*fr;
            curU[x]=U;curV[x]=V;
            if(!m_prevLineU.empty()&&x<(int)m_prevLineU.size()){U=(U+m_prevLineU[x])*0.5f;V=(V+m_prevLineV[x])*0.5f;}
            U*=m_chromaGain;V*=m_chromaGain;
        }
        uint8_t r,g,b;yuv2rgb(Y,U,V,r,g,b);
        if(m_videoInvert){r=255-r;g=255-g;b=255-b;}
        rp[x*4]=r;rp[x*4+1]=g;rp[x*4+2]=b;rp[x*4+3]=255;
    }
    m_prevLineU=curU;m_prevLineV=curV;
}

void PALDecoder::buildFrame() {
    m_frameCount++;
    if(m_frameCallback)m_frameCallback(m_frameBuffer.data(),VIDEO_WIDTH,VIDEO_HEIGHT);
}

void PALDecoder::yuv2rgb(float y, float u, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    r=(uint8_t)clip((y+1.14f*v)*255,0,255);
    g=(uint8_t)clip((y-0.396f*u-0.581f*v)*255,0,255);
    b=(uint8_t)clip((y+2.029f*u)*255,0,255);
}
