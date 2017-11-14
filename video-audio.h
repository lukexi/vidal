#ifndef VIDEO_AUDIO_H
#define VIDEO_AUDIO_H

#include <portaudio.h>
#include "pa_ringbuffer.h"

#define SAMPLE_RATE 44100
#define BLOCK_SIZE 128

#define AUDIO_QUEUE 16 // must be power of 2

#define NUM_CHANNELS 16

typedef struct {
    float* Samples;
    int Length;
    int NextSampleIndex;
} audio_block;

typedef struct {
    PaUtilRingBuffer BlocksRingBuf;
    void*            BlocksRingBufStorage;
    int ReadBlockIndex;
    int WriteBlockIndex;
    audio_block Blocks[AUDIO_QUEUE];
} audio_channel;

typedef struct {
    audio_channel Channels[NUM_CHANNELS];
    int NextChannel;
    PaStream* Stream;
} audio_state;

audio_state* StartAudio();

int GetNextChannel(audio_state* AudioState);

#endif // VIDEO_AUDIO_H
