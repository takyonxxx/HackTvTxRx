#include "DSPBridge.h"
#include "PALDecoder.h"
#include "AudioDemodulator.h"

// ============================================================
// PAL Decoder Bridge
// ============================================================

struct PALDecoderWrapper {
    PALDecoder decoder;
    FrameCallbackC frameCallback = nullptr;
    void* frameContext = nullptr;
    SyncStatsCallbackC syncCallback = nullptr;
    void* syncContext = nullptr;
};

PALDecoderRef palDecoder_create(void) {
    auto* w = new PALDecoderWrapper();
    return static_cast<PALDecoderRef>(w);
}

void palDecoder_destroy(PALDecoderRef ref) {
    delete static_cast<PALDecoderWrapper*>(ref);
}

void palDecoder_setFrameCallback(PALDecoderRef ref, FrameCallbackC cb, void* context) {
    auto* w = static_cast<PALDecoderWrapper*>(ref);
    w->frameCallback = cb;
    w->frameContext = context;
    w->decoder.setFrameCallback([w](const uint8_t* rgba, int width, int height) {
        if (w->frameCallback)
            w->frameCallback(rgba, width, height, w->frameContext);
    });
}

void palDecoder_setSyncStatsCallback(PALDecoderRef ref, SyncStatsCallbackC cb, void* context) {
    auto* w = static_cast<PALDecoderWrapper*>(ref);
    w->syncCallback = cb;
    w->syncContext = context;
    w->decoder.setSyncStatsCallback([w](float syncRate, float peak, float min) {
        if (w->syncCallback)
            w->syncCallback(syncRate, peak, min, w->syncContext);
    });
}

void palDecoder_processSamples(PALDecoderRef ref, const int8_t* data, size_t len) {
    static_cast<PALDecoderWrapper*>(ref)->decoder.processSamples(data, len);
}

void palDecoder_setSampleRate(PALDecoderRef ref, int sampleRate) {
    static_cast<PALDecoderWrapper*>(ref)->decoder.setSampleRate(sampleRate);
}

void palDecoder_setTuneFrequency(PALDecoderRef ref, uint64_t freqHz) {
    static_cast<PALDecoderWrapper*>(ref)->decoder.setTuneFrequency(freqHz);
}

void palDecoder_setVideoGain(PALDecoderRef ref, float gain) {
    static_cast<PALDecoderWrapper*>(ref)->decoder.setVideoGain(gain);
}

void palDecoder_setVideoOffset(PALDecoderRef ref, float offset) {
    static_cast<PALDecoderWrapper*>(ref)->decoder.setVideoOffset(offset);
}

void palDecoder_setVideoInvert(PALDecoderRef ref, int invert) {
    static_cast<PALDecoderWrapper*>(ref)->decoder.setVideoInvert(invert != 0);
}

void palDecoder_setColorMode(PALDecoderRef ref, int color) {
    static_cast<PALDecoderWrapper*>(ref)->decoder.setColorMode(color != 0);
}

void palDecoder_setChromaGain(PALDecoderRef ref, float gain) {
    static_cast<PALDecoderWrapper*>(ref)->decoder.setChromaGain(gain);
}

void palDecoder_setSyncThreshold(PALDecoderRef ref, float threshold) {
    static_cast<PALDecoderWrapper*>(ref)->decoder.setSyncThreshold(threshold);
}

// ============================================================
// Audio Demodulator Bridge
// ============================================================

struct AudioDemodWrapper {
    AudioDemodulator demod;
    AudioCallbackC audioCallback = nullptr;
    void* audioContext = nullptr;
};

AudioDemodRef audioDemod_create(void) {
    auto* w = new AudioDemodWrapper();
    return static_cast<AudioDemodRef>(w);
}

void audioDemod_destroy(AudioDemodRef ref) {
    delete static_cast<AudioDemodWrapper*>(ref);
}

void audioDemod_setAudioCallback(AudioDemodRef ref, AudioCallbackC cb, void* context) {
    auto* w = static_cast<AudioDemodWrapper*>(ref);
    w->audioCallback = cb;
    w->audioContext = context;
    w->demod.setAudioCallback([w](const float* samples, size_t count) {
        if (w->audioCallback)
            w->audioCallback(samples, count, w->audioContext);
    });
}

void audioDemod_processSamples(AudioDemodRef ref, const int8_t* data, size_t len) {
    static_cast<AudioDemodWrapper*>(ref)->demod.processSamples(data, len);
}

void audioDemod_setSampleRate(AudioDemodRef ref, double sampleRate) {
    static_cast<AudioDemodWrapper*>(ref)->demod.setSampleRate(sampleRate);
}

void audioDemod_setAudioCarrierFreq(AudioDemodRef ref, double freqHz) {
    static_cast<AudioDemodWrapper*>(ref)->demod.setAudioCarrierFreq(freqHz);
}

void audioDemod_setAudioGain(AudioDemodRef ref, float gain) {
    static_cast<AudioDemodWrapper*>(ref)->demod.setAudioGain(gain);
}

void audioDemod_setAudioEnabled(AudioDemodRef ref, int enabled) {
    static_cast<AudioDemodWrapper*>(ref)->demod.setAudioEnabled(enabled != 0);
}

int audioDemod_isAudioCapable(AudioDemodRef ref) {
    return static_cast<AudioDemodWrapper*>(ref)->demod.isAudioCapable() ? 1 : 0;
}
