
#include "ringbuffer.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
void CreateRingBuffer(
    ringbuffer* RingBufferOut,
    ring_buffer_size_t ElementSizeBytes,
    ring_buffer_size_t ElementCount) {

    RingBufferOut->Storage = malloc(ElementSizeBytes * ElementCount);
    if (RingBufferOut->Storage == NULL) {
        printf("Ring buffer malloc of %i bytes failed\n", (int)(ElementSizeBytes * ElementCount));
        exit(1);
    }

    ring_buffer_size_t Result = PaUtil_InitializeRingBuffer(
        &RingBufferOut->RingBuffer,
        ElementSizeBytes,
        ElementCount,
        RingBufferOut->Storage);
    if (Result != 0) {
        printf("Ring buffer count not a power of 2 (%i)\n", (int)ElementCount);
        exit(1);
    }
}

ring_buffer_size_t GetRingBufferReadAvailable(ringbuffer* RingBuffer) {
    return PaUtil_GetRingBufferReadAvailable(&RingBuffer->RingBuffer);
}

ring_buffer_size_t GetRingBufferWriteAvailable(ringbuffer* RingBuffer) {
    return PaUtil_GetRingBufferWriteAvailable(&RingBuffer->RingBuffer);
}


void ReadRingBuffer(ringbuffer* RingBuffer,
    void* Result,
    ring_buffer_size_t ElementCount)
{
    PaUtil_ReadRingBuffer(
        &RingBuffer->RingBuffer,
        Result,
        ElementCount);
}

ring_buffer_size_t WriteRingBuffer(ringbuffer* RingBuffer,
    const void* Data,
    ring_buffer_size_t ElementCount)
{
    return PaUtil_WriteRingBuffer(
        &RingBuffer->RingBuffer,
        Data,
        ElementCount);
}

void FreeRingBuffer(ringbuffer* RingBuffer) {
    free(RingBuffer->Storage);
}
