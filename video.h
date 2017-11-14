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


#define QUEUE_FRAMES 60

typedef struct {
    AVFrame* Frame;
    double PTS;
    bool Presented;
} queued_frame;

typedef struct {
    int Index;
    queued_frame FrameQueue[QUEUE_FRAMES];
    int ReadHead;
    int WriteHead;
    AVCodec*           Codec;
    AVCodecContext*    CodecContext;
    double Timebase;
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
    int AudioChannel;
} video;

video* OpenVideo(const char* InputFilename, NVGcontext* NVG, audio_state* AudioState);

bool TickVideo(video* Video, audio_state* AudioState);
void SeekVideo(video* Video, double Timestamp);
void FreeVideo(video* Video, NVGcontext* NVG);

#endif // VIDEO_H
