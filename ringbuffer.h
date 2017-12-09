#ifndef RINGBUFFER_H
#define RINGBUFFER_H
#include "pa_ringbuffer.h"
typedef struct {
    PaUtilRingBuffer RingBuffer;
    void*            Storage;
} ringbuffer;

void CreateRingBuffer(
    ringbuffer* RingBufferOut,
    ring_buffer_size_t ElementSizeBytes,
    ring_buffer_size_t ElementCount);
ring_buffer_size_t GetRingBufferReadAvailable(ringbuffer* RingBuffer);
ring_buffer_size_t GetRingBufferWriteAvailable(ringbuffer* RingBuffer);
void ReadRingBuffer(ringbuffer* RingBuffer, void* Result, ring_buffer_size_t ElementCount);
ring_buffer_size_t WriteRingBuffer(ringbuffer *RingBuffer, const void *Data, ring_buffer_size_t ElementCount);

#endif // RINGBUFFER_H
