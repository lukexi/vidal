#ifndef VIDEO_AUDIO_H
#define VIDEO_AUDIO_H

#include "ringbuffer.h"
#include <jack/jack.h>

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
    ringbuffer BlocksIn;
    int ReadBlockIndex;
    int WriteBlockIndex;
    audio_block Blocks[AUDIO_QUEUE];
} audio_channel;

typedef struct {
    audio_channel Channels[NUM_CHANNELS];
    int NextChannel;
    jack_port_t *OutputPortLeft;
    jack_port_t *OutputPortRight;
    jack_client_t *Client;
} audio_state;

audio_state* StartAudio();

int GetNextChannel(audio_state* AudioState);

#endif // VIDEO_AUDIO_H
