#ifndef VIDEO_H
#define VIDEO_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <GL/glew.h>
#include "nanovg.h"
#include "video-audio.h"
#include <stdbool.h>
#include "mvar.h"

typedef struct {
    bool               Valid;
    int                Index;
    ringbuffer         Buffer;
    AVCodec*           Codec;
    AVCodecContext*    CodecContext;
    AVStream*          Stream;
    double             Timebase;
} stream;

typedef struct {

    AVPacket           Packet;
    AVFormatContext*   FormatContext;

    stream AudioStream;
    stream VideoStream;

    int Width;
    int Height;

    bool EndOfStream;

    struct SwsContext* ColorConvertContext;
    size_t ColorConvertBufferSize;
    uint8_t* ColorConvertBuffer;

    GLuint   Texture;
    int      NVGImage;

    double StartTime;

    NVGcontext* NVG;
    int AudioChannel;
    audio_state* AudioState;

    pthread_t DecodeThread;
    bool StopDecodeThread;
} video;

// These functions should only be called
// from a single thread which has an OpenGL
// context.

video* OpenVideo(const char* InputFilename, NVGcontext* NVG, audio_state* AudioState);

void FreeVideo(video* Video);

// Uploads a frame to the graphics
// card when that frame's presentation
// time arrives.
// Should be called as fast as possible.
void TickVideo(video* Video);

#endif // VIDEO_H
