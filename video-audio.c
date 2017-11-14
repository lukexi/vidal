#include "video-audio.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
void PrintPortAudioError(PaError err) {
    printf("PortAudio error: %s\n", Pa_GetErrorText(err));
}

void CreateRingBuffer(
    ring_buffer_size_t ElementSizeBytes,
    ring_buffer_size_t ElementCount,
    void* DataPointer,
    PaUtilRingBuffer* RingBufferOut) {

    DataPointer = malloc(ElementSizeBytes * ElementCount);
    if (DataPointer == NULL) {
        printf("Ring buffer malloc of %i bytes failed\n", ElementSizeBytes * ElementCount);
        exit(1);
    }

    ring_buffer_size_t Result = PaUtil_InitializeRingBuffer(
        RingBufferOut,
        ElementSizeBytes,
        ElementCount,
        DataPointer);
    if (Result != 0) {
        printf("Ring buffer count not a power of 2 (%i)\n", ElementCount);
        exit(1);
    }
}

int AudioThreadCallback(
    const void *InputBuffer,
    void *OutputBuffer,
    unsigned long SamplesPerBlock,
    const PaStreamCallbackTimeInfo* TimeInfo,
    PaStreamCallbackFlags StatusFlags,
    void *UserData) {

    audio_state *S = (audio_state*)UserData;
    ring_buffer_size_t NumNewBlocks =
        PaUtil_GetRingBufferReadAvailable(
            &S->BlocksRingBuf);
    for (int Index = 0; Index < NumNewBlocks; Index++) {
        audio_block* NewBlock = &S->Blocks[S->WriteBlockIndex];
        PaUtil_ReadRingBuffer(
            &S->BlocksRingBuf,
            NewBlock,
            1);
        S->WriteBlockIndex = (S->WriteBlockIndex + 1) % AUDIO_QUEUE;
    }

    audio_block* Block = &S->Blocks[S->ReadBlockIndex];
    // printf("CALLBACK BEGAN: BlockID: %i %i %i\n", Block->BlockID, Block->Length, S->ReadBlockIndex);

    float *Out = (float*)OutputBuffer;
    for (int SampleIndex = 0; SampleIndex < SamplesPerBlock; SampleIndex++) {

        float Amp = 0;

        // Find a valid block to read from
        bool OKToRead = false;
        for (int Tries = 0; Tries < AUDIO_QUEUE; Tries++) {
            if (Block->NextSampleIndex < Block->Length) {
                OKToRead = true;
                break;
            }
            else {
                // FIXME: should do this on the main thread
                free(Block->Samples);
                Block->Samples = NULL;

                S->ReadBlockIndex = (S->ReadBlockIndex + 1) % AUDIO_QUEUE;
                Block = &S->Blocks[S->ReadBlockIndex];
            }
        }

        if (OKToRead) {
            // printf("BlockID: %i S: %i\n", Block->BlockID, Block->NextSampleIndex);
            Amp = Block->Samples[Block->NextSampleIndex];
            Block->NextSampleIndex++;
        }

        *Out++ = Amp;
        *Out++ = Amp;
    }

    return 0;
}



audio_state* StartAudio() {
    PaError Err;
    Err = Pa_Initialize();
    if (Err != paNoError) { PrintPortAudioError(Err); return NULL; }

    audio_state* AudioState = calloc(1, sizeof(audio_state));

    const int RingBufSize = AUDIO_QUEUE; // Size must be power of 2

    CreateRingBuffer(sizeof(audio_block), RingBufSize, AudioState->BlocksRingBufStorage, &AudioState->BlocksRingBuf);

    if (Pa_GetDeviceCount() == 0) { return NULL; }

    PaDeviceIndex OutputDeviceIndex = Pa_GetDefaultOutputDevice();

    const PaDeviceInfo* OutputDeviceInfo = Pa_GetDeviceInfo(OutputDeviceIndex);

    PaTime OutputDeviceLatency = OutputDeviceInfo->defaultLowOutputLatency;

    PaStreamParameters OutputParameters = {
        .device = OutputDeviceIndex,
        .channelCount = 2,
        .sampleFormat = paFloat32,
        .suggestedLatency = OutputDeviceLatency,
        .hostApiSpecificStreamInfo = NULL
    };

    PaStream *Stream;
    /* Open an audio I/O stream. */
    Err = Pa_OpenStream(
        &Stream,
        NULL,
        &OutputParameters,
        SAMPLE_RATE,            // Samples per second
        BLOCK_SIZE,             // Samples per block (will be * the num channels)
        paNoFlag,               // PaStreamFlags
        AudioThreadCallback,    // Your callback function
        AudioState);            // A pointer that will be passed to your callback
    if (Err != paNoError) { PrintPortAudioError(Err); return NULL; }

    Err = Pa_StartStream(Stream);
    if (Err != paNoError) { PrintPortAudioError(Err); return NULL; }

    return AudioState;
}
