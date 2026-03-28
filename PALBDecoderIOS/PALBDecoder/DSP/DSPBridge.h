#ifndef DSPBRIDGE_H
#define DSPBRIDGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// PAL Decoder
typedef void* PALDecoderRef;
typedef void (*FrameCallbackC)(const uint8_t* rgba, int width, int height, void* context);
typedef void (*SyncStatsCallbackC)(float syncRate, float peak, float min, void* context);

PALDecoderRef palDecoder_create(void);
void palDecoder_destroy(PALDecoderRef ref);
void palDecoder_setFrameCallback(PALDecoderRef ref, FrameCallbackC cb, void* context);
void palDecoder_setSyncStatsCallback(PALDecoderRef ref, SyncStatsCallbackC cb, void* context);
void palDecoder_processSamples(PALDecoderRef ref, const int8_t* data, size_t len);
void palDecoder_setSampleRate(PALDecoderRef ref, int sampleRate);
void palDecoder_setTuneFrequency(PALDecoderRef ref, uint64_t freqHz);
void palDecoder_setVideoGain(PALDecoderRef ref, float gain);
void palDecoder_setVideoOffset(PALDecoderRef ref, float offset);
void palDecoder_setVideoInvert(PALDecoderRef ref, int invert);
void palDecoder_setColorMode(PALDecoderRef ref, int color);
void palDecoder_setChromaGain(PALDecoderRef ref, float gain);
void palDecoder_setSyncThreshold(PALDecoderRef ref, float threshold);

// Audio Demodulator
typedef void* AudioDemodRef;
typedef void (*AudioCallbackC)(const float* samples, size_t count, void* context);

AudioDemodRef audioDemod_create(void);
void audioDemod_destroy(AudioDemodRef ref);
void audioDemod_setAudioCallback(AudioDemodRef ref, AudioCallbackC cb, void* context);
void audioDemod_processSamples(AudioDemodRef ref, const int8_t* data, size_t len);
void audioDemod_setSampleRate(AudioDemodRef ref, double sampleRate);
void audioDemod_setAudioCarrierFreq(AudioDemodRef ref, double freqHz);
void audioDemod_setAudioGain(AudioDemodRef ref, float gain);
void audioDemod_setAudioEnabled(AudioDemodRef ref, int enabled);
int audioDemod_isAudioCapable(AudioDemodRef ref);
void audioDemod_setRadioMode(AudioDemodRef ref, int radio);
// Demod mode: 0=FM, 1=AM
void audioDemod_setDemodMode(AudioDemodRef ref, int mode);
int audioDemod_getDemodMode(AudioDemodRef ref);

#ifdef __cplusplus
}
#endif

#endif
