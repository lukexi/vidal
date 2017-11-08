#include <SDL.h>
#include <GL/glew.h>
#include <stdbool.h>
#include "shader.h"
#include "quad.h"
#include "texture.h"

/**
 * H264 codec test.
 */

#include "libavutil/adler32.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"

#include "libavutil/adler32.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"


typedef struct {
    AVFrame*           Frame;
    AVPacket           Packet;
    AVFormatContext*   FormatContext;

    int VideoStream;
    AVCodec*           VideoCodec;
    AVCodecContext*    VideoCodecContext;

    int AudioStream;
    AVCodec*           AudioCodec;
    AVCodecContext*    AudioCodecContext;

    int Width;
    int Height;
    int FrameNumber;
    bool EndOfStream;
} video;


void RenderFrame(video* Video,
    SDL_Window* Window, GLuint QuadProgram, GLuint Quad,
    GLuint YTex, GLuint UTex, GLuint VTex)
{
    UpdateTexture(YTex, Video->Width,     Video->Height,     GL_RED, Video->Frame->data[0], Video->Frame->linesize[0]); // Y pixels
    UpdateTexture(UTex, Video->Width*0.5, Video->Height*0.5, GL_RED, Video->Frame->data[1], Video->Frame->linesize[1]); // U pixels
    UpdateTexture(VTex, Video->Width*0.5, Video->Height*0.5, GL_RED, Video->Frame->data[2], Video->Frame->linesize[2]); // V pixels

    glClearColor(0, 0.1, 0.1, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform1i(glGetUniformLocation(QuadProgram, "uTexY"), 0);
    glUniform1i(glGetUniformLocation(QuadProgram, "uTexU"), 1);
    glUniform1i(glGetUniformLocation(QuadProgram, "uTexV"), 2);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, YTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, UTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, VTex);

    glBindVertexArray(Quad);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    SDL_GL_SwapWindow(Window);
    SDL_Event Event;
    while (SDL_PollEvent(&Event)) {
        if (Event.type == SDL_QUIT) exit(0);
    }
}

 bool OpenCodec(
    enum AVMediaType MediaType,
    AVFormatContext* FormatContext,
    int* StreamIndex,
    AVCodec** Codec,
    AVCodecContext** CodecContext)
{
    int Result = 0;

    *StreamIndex = av_find_best_stream(FormatContext, MediaType, -1, -1, NULL, 0);
    if (*StreamIndex < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't find stream in input file\n");
        return false;
    }
    printf("Assigned stream index %i\n", *StreamIndex);

    AVCodecParameters* CodecParams = FormatContext->streams[*StreamIndex]->codecpar;
    *Codec = avcodec_find_decoder(CodecParams->codec_id);
    if (*Codec == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Can't find decoder\n");
        return false;
    }

    *CodecContext = avcodec_alloc_context3(*Codec);
    if (*CodecContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate decoder context\n");
        // AVERROR(ENOMEM);
        return false;
    }

    Result = avcodec_parameters_to_context(*CodecContext, CodecParams);
    if (Result) {
        av_log(NULL, AV_LOG_ERROR, "Can't copy decoder context\n");
        return false;
    }

    Result = avcodec_open2(*CodecContext, *Codec, NULL);
    if (Result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't open decoder\n");
        return false;
    }

    return true;
}

video* OpenVideo(const char* InputFilename) {
    video* Video = calloc(1, sizeof(video));

    int Result;

    Result = avformat_open_input(&Video->FormatContext, InputFilename, NULL, NULL);
    if (Result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't open file\n");
        free(Video);
        return NULL;
    }

    Result = avformat_find_stream_info(Video->FormatContext, NULL);
    if (Result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't get stream info\n");
        free(Video);
        return NULL;
    }

    bool FoundAudio = OpenCodec(AVMEDIA_TYPE_AUDIO,
        Video->FormatContext,
        &Video->AudioStream,
        &Video->AudioCodec,
        &Video->AudioCodecContext
        );

    bool FoundVideo = OpenCodec(AVMEDIA_TYPE_VIDEO,
        Video->FormatContext,
        &Video->VideoStream,
        &Video->VideoCodec,
        &Video->VideoCodecContext
        );
    printf("Video stream index: %i\n", Video->VideoStream);
    printf("Audio stream index: %i\n", Video->AudioStream);


    Video->Frame = av_frame_alloc();
    if (!Video->Frame) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate frame\n");
        // return AVERROR(ENOMEM);
        free(Video);
        return NULL;
    }

    Video->Width  = Video->VideoCodecContext->width;
    Video->Height = Video->VideoCodecContext->height;

    return Video;
}

int GetFrame(video* Video) {

    int Result;

    bool GotFrame = false;

    AVRational Timebase =
        Video->FormatContext->streams[Video->VideoStream]->time_base;
    printf("Timebase for video stream %d: %d/%d\n",
        Video->VideoStream, Timebase.num, Timebase.den);

    av_init_packet(&Video->Packet);

    if (!Video->EndOfStream) {
        int Result = av_read_frame(Video->FormatContext, &Video->Packet);
        if (Result < 0) {
            Video->EndOfStream = 1;
        }
    }

    // If at end of stream, begin flush mode by sending a NULL packet
    if (Video->EndOfStream) {
        Video->Packet.data = NULL;
        Video->Packet.size = 0;
    }

    int StreamIndex = Video->Packet.stream_index;
    AVCodec*        Codec = NULL;
    AVCodecContext* CodecContext = NULL;
    if (StreamIndex == Video->AudioStream) {
        Codec        = Video->AudioCodec;
        CodecContext = Video->AudioCodecContext;
        printf("GOT A AUDIO FRAME %i\n", Video->Packet.stream_index);
    } else if (StreamIndex == Video->VideoStream) {
        Codec        = Video->VideoCodec;
        CodecContext = Video->VideoCodecContext;
        printf("GOT A VIDEO FRAME\n");
    } else {
        printf("Unknown stream index %i\n", StreamIndex);
        av_packet_unref(&Video->Packet);
        av_init_packet(&Video->Packet);
        return -1;
    }

    Result = avcodec_send_packet(CodecContext, &Video->Packet);
    if (Result != 0) {

        av_log(NULL, AV_LOG_ERROR, "Error sending packet\n");
        return Result;
    }

    Result = avcodec_receive_frame(CodecContext, Video->Frame);
    if (Result != 0 && Result != AVERROR_EOF) {
        av_log(NULL, AV_LOG_ERROR, "Error receiving frame\n");
        return Result;
    }

    if (Result == 0) {
        GotFrame = true;
    }

    av_packet_unref(&Video->Packet);
    av_init_packet(&Video->Packet);

    if (GotFrame) {
        return StreamIndex;
    } else {
        return -1;
    }
}

void FreeVideo(video* Video) {
    av_packet_unref(&Video->Packet);
    av_frame_free(&Video->Frame);
    avcodec_close(Video->VideoCodecContext);
    avcodec_close(Video->AudioCodecContext);
    avformat_close_input(&Video->FormatContext);
    avcodec_free_context(&Video->VideoCodecContext);
    avcodec_free_context(&Video->AudioCodecContext);
}

#define NUM_QUADS 1

int main(int argc, char const *argv[]) {
    av_register_all();

    SDL_Init(SDL_INIT_VIDEO);

    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* Window = SDL_CreateWindow("Veil", 10,10, 1024,1024, SDL_WINDOW_OPENGL);
    SDL_GLContext GLContext = SDL_GL_CreateContext(Window);
    SDL_GL_MakeCurrent(Window, GLContext);
    InitGLEW();

    video* Video = OpenVideo("pinball.mov");


    GLuint QuadProgram = CreateVertFragProgramFromPath(
        "quad.vert",
        "quad.frag");
    glUseProgram(QuadProgram);

    GLuint YTex = CreateTexture(Video->Width,     Video->Height,     1);
    GLuint UTex = CreateTexture(Video->Width*0.5, Video->Height*0.5, 1);
    GLuint VTex = CreateTexture(Video->Width*0.5, Video->Height*0.5, 1);

    float Verts[8] = {
        -1, -1, // Left Top
        -1, 1,  // Left Bottom
        1, -1,  // Right Top
        1, 1    // Right Bottom
    };
    GLuint Quad = CreateQuad(Verts);

    while (!Video->EndOfStream) {
        int StreamIndex = GetFrame(Video);
        printf("Stream index; %i\n", StreamIndex);
        if (StreamIndex == Video->VideoStream) {
            RenderFrame(Video, Window, QuadProgram, Quad, YTex, UTex, VTex);
        }
    }
    // printf("%10"PRId64", %10"PRId64", %8"PRId64"\n",
    //     Video->Frame->pts, Video->Frame->pkt_dts, Video->Frame->pkt_duration);
    // printf("Uploading %s frame of %i x %i\n",
    //     av_get_pix_fmt_name(Video->CodecContext->pix_fmt),
    //     Video->CodecContext->width, Video->CodecContext->height);

    return 0;
}
