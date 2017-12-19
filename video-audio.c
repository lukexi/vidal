#include "video-audio.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "ringbuffer.h"

int AudioThreadCallback(
    jack_nframes_t NumFrames, void *Arg) {

    audio_state *S = (audio_state*)Arg;
    jack_default_audio_sample_t *OutputBufferLeft  = jack_port_get_buffer(S->OutputPortLeft,  NumFrames);
    jack_default_audio_sample_t *OutputBufferRight = jack_port_get_buffer(S->OutputPortRight, NumFrames);

    // Write silence
    float *OutLeft  = (float*)OutputBufferLeft;
    float *OutRight = (float*)OutputBufferRight;
    for (int SampleIndex = 0; SampleIndex < NumFrames; SampleIndex++) {
        *OutLeft++ = 0;
        *OutRight++ = 0;
    }

    for (int ChannelIndex = 0; ChannelIndex < NUM_CHANNELS; ChannelIndex++) {
        audio_channel* Ch = &S->Channels[ChannelIndex];

        ring_buffer_size_t NumNewBlocks =
            GetRingBufferReadAvailable(
                &Ch->BlocksIn);
        for (int Index = 0; Index < NumNewBlocks; Index++) {
            audio_block* NewBlock = &Ch->Blocks[Ch->WriteBlockIndex];
            ReadRingBuffer(
                &Ch->BlocksIn,
                NewBlock,
                1);
            Ch->WriteBlockIndex = (Ch->WriteBlockIndex + 1) % AUDIO_QUEUE;
        }

        audio_block* Block = &Ch->Blocks[Ch->ReadBlockIndex];

        float *OutLeft  = (float*)OutputBufferLeft;
        float *OutRight = (float*)OutputBufferRight;
        bool FoundSomethin = false;
        for (int SampleIndex = 0; SampleIndex < NumFrames; SampleIndex++) {

            float Amp = 0;

            // Find a valid block to read from
            bool OKToRead = false;
            for (int Tries = 0; Tries < AUDIO_QUEUE; Tries++) {
                if (Block->NextSampleIndex < Block->Length) {
                    OKToRead = true;
                    FoundSomethin = true;
                    break;
                }
                else {
                    // FIXME: should do this on the main thread
                    free(Block->Samples);
                    Block->Samples = NULL;

                    Ch->ReadBlockIndex = (Ch->ReadBlockIndex + 1) % AUDIO_QUEUE;
                    Block = &Ch->Blocks[Ch->ReadBlockIndex];
                }
            }

            if (OKToRead) {
                Amp = Block->Samples[Block->NextSampleIndex];
                Block->NextSampleIndex++;
            }

            *OutLeft++  += Amp;
            *OutRight++ += Amp;
        }

        if (!FoundSomethin) {
            // if (ChannelIndex == 0) printf("AUDIO THREAD STARVED\n");
        }
    }

    return 0;
}

int GetNextChannel(audio_state* AudioState) {
    int Next = AudioState->NextChannel;
    AudioState->NextChannel = (AudioState->NextChannel + 1) % NUM_CHANNELS;
    return Next;
}

bool StartJack(audio_state* AudioState) {
    const char **Ports;
    const char *ClientName = "VideoAudioEngine";
    const char *ServerName = NULL;
    jack_options_t Options = JackNullOption;
    jack_status_t Status;

    AudioState->Client = jack_client_open(ClientName, Options, &Status, ServerName);
    if (AudioState->Client == NULL) {
        fprintf(stderr, "jack_client_open() failed, "
             "status = 0x%2.0x\n", Status);
        if (Status & JackServerFailed) {
            fprintf(stderr, "Unable to connect to JACK server\n");
        }
        return false;
    }
    if (Status & JackServerStarted) {
        fprintf(stderr, "JACK server started\n");
    }

    if (Status & JackNameNotUnique) {
        ClientName = jack_get_client_name(AudioState->Client);
        fprintf(stderr, "unique name `%s' assigned\n", ClientName);
    }


    jack_set_process_callback(AudioState->Client, AudioThreadCallback, (void*)AudioState);
    // jack_on_shutdown(client, jack_shutdown, 0);

    printf("engine sample rate: %" PRIu32 "\n",
        jack_get_sample_rate(AudioState->Client));

    AudioState->OutputPortLeft = jack_port_register(AudioState->Client,  "output_left",
                      JACK_DEFAULT_AUDIO_TYPE,
                      JackPortIsOutput|JackPortIsTerminal, 0);
    AudioState->OutputPortRight = jack_port_register(AudioState->Client, "output_right",
                      JACK_DEFAULT_AUDIO_TYPE,
                      JackPortIsOutput|JackPortIsTerminal, 0);

    if (AudioState->OutputPortLeft == NULL || AudioState->OutputPortRight == NULL) {
        fprintf(stderr, "no more JACK ports available\n");
        return false;
    }

    if (jack_activate(AudioState->Client)) {
        fprintf(stderr, "cannot activate client");
        return false;
    }

    // "Input" here meaning we are "Inputting to JACK",
    Ports = jack_get_ports(AudioState->Client, NULL, NULL, JackPortIsPhysical|JackPortIsInput);
    if (Ports == NULL) {
        fprintf(stderr, "no physical playback ports\n");
        return false;
    }

    if (jack_connect(AudioState->Client, jack_port_name(AudioState->OutputPortLeft), Ports[0])
        || jack_connect(AudioState->Client, jack_port_name(AudioState->OutputPortRight), Ports[1])) {
        fprintf(stderr, "cannot connect output ports\n");
        free(Ports);
        return false;
    }

    free(Ports);
    return true;
}

audio_state* StartAudio() {

    audio_state* AudioState = calloc(1, sizeof(audio_state));

    const int RingBufSize = AUDIO_QUEUE; // Size must be power of 2

    for (int ChannelIndex = 0; ChannelIndex < NUM_CHANNELS; ChannelIndex++) {
        audio_channel* Ch = &AudioState->Channels[ChannelIndex];
        CreateRingBuffer(&Ch->BlocksIn, sizeof(audio_block), RingBufSize);
    }

    bool JackStarted = StartJack(AudioState);
    if (!JackStarted) {
        free(AudioState);
        return NULL;
    }

    return AudioState;
}
