#ifndef VIDEO_AUDIO_H
#define VIDEO_AUDIO_H

#include <portaudio.h>
#include "pa_ringbuffer.h"

#define SAMPLE_RATE 44100
#define BLOCK_SIZE 128

#define AUDIO_QUEUE 16 // must be power of 2

typedef struct {
    float* Samples;
    int Length;
    int NextSampleIndex;
    int BlockID;
} audio_block;

typedef struct {
    PaUtilRingBuffer BlocksRingBuf;
    void*            BlocksRingBufStorage;
    int ReadBlockIndex;
    int WriteBlockIndex;
    audio_block Blocks[AUDIO_QUEUE];
    PaStream* Stream;
} audio_state;

audio_state* StartAudio();

#endif // VIDEO_AUDIO_H
