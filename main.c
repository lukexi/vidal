#include <SDL.h>
#include <GL/glew.h>
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

void RenderFrame(AVCodecContext* CodecContext, AVFrame* Frame,
    SDL_Window* Window, GLuint QuadProgram, GLuint Quad,
    GLuint YTex, GLuint UTex, GLuint VTex)
{
    UpdateTexture(YTex, CodecContext->width, CodecContext->height, GL_RED, Frame->data[0], Frame->linesize[0]); // Y pixels
    UpdateTexture(UTex, CodecContext->width*0.5, CodecContext->height*0.5, GL_RED, Frame->data[1], Frame->linesize[1]); // U pixels
    UpdateTexture(VTex, CodecContext->width*0.5, CodecContext->height*0.5, GL_RED, Frame->data[2], Frame->linesize[2]); // V pixels

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

typedef struct {
    AVCodec*         Codec;
    AVFrame*         Frame;
    AVPacket         Packet;
    AVCodecParameters* CodecParams;
    AVCodecContext*  CodecContext;
    AVFormatContext* FormatContext;
    int VideoStream;
    int AudioStream;
} video;

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

    Video->VideoStream = av_find_best_stream(Video->FormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (Video->VideoStream < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't find video stream in input file\n");
        free(Video);
        return NULL;
    }
    Video->AudioStream = av_find_best_stream(Video->FormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

    Video->CodecParams = Video->FormatContext->streams[Video->VideoStream]->codecpar;

    Video->Codec = avcodec_find_decoder(Video->CodecParams->codec_id);
    if (!Video->Codec) {
        av_log(NULL, AV_LOG_ERROR, "Can't find decoder\n");
        free(Video);
        return NULL;
    }

    Video->CodecContext = avcodec_alloc_context3(Video->Codec);
    if (!Video->CodecContext) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate decoder context\n");
        // AVERROR(ENOMEM);
        free(Video);
        return NULL;
    }

    Result = avcodec_parameters_to_context(Video->CodecContext, Video->CodecParams);
    if (Result) {
        av_log(NULL, AV_LOG_ERROR, "Can't copy decoder context\n");
        free(Video);
        return NULL;
    }

    Result = avcodec_open2(Video->CodecContext, Video->Codec, NULL);
    if (Result < 0) {
        av_log(Video->CodecContext, AV_LOG_ERROR, "Can't open decoder\n");
        free(Video);
        return NULL;
    }

    Video->Frame = av_frame_alloc();
    if (!Video->Frame) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate frame\n");
        // return AVERROR(ENOMEM);
        free(Video);
        return NULL;
    }
    return Video;
}

static int VideoDecodeExample(video* Video, SDL_Window* Window) {

    int GotFrame = 0;
    int FrameNumber = 0;
    int Result;

    int EndOfStream = 0;
    int EndOfFile = 0;

    GLuint QuadProgram = CreateVertFragProgramFromPath(
        "quad.vert",
        "quad.frag");
    glUseProgram(QuadProgram);

    GLuint YTex = CreateTexture(Video->CodecContext->width,     Video->CodecContext->height,     1);
    GLuint UTex = CreateTexture(Video->CodecContext->width*0.5, Video->CodecContext->height*0.5, 1);
    GLuint VTex = CreateTexture(Video->CodecContext->width*0.5, Video->CodecContext->height*0.5, 1);

    float Verts[8] = {
        -1, -1, // Left Top
        -1, 1,  // Left Bottom
        1, -1,  // Right Top
        1, 1    // Right Bottom
    };
    GLuint Quad = CreateQuad(Verts);

    AVRational Timebase = Video->FormatContext->streams[Video->VideoStream]->time_base;
    printf("Timebase for video stream %d: %d/%d\n", Video->VideoStream, Timebase.num, Timebase.den);
    FrameNumber = 0;
    av_init_packet(&Video->Packet);
    do {
        if (!EndOfStream) {
            if (av_read_frame(Video->FormatContext, &Video->Packet) < 0) {
                EndOfStream = 1;
            }
        }
        if (EndOfStream) {
            Video->Packet.data = NULL;
            Video->Packet.size = 0;
        }
        if (Video->Packet.stream_index == Video->AudioStream || EndOfStream) {

        }
        if (Video->Packet.stream_index == Video->VideoStream || EndOfStream) {
            GotFrame = 0;

            if (Video->Packet.pts == AV_NOPTS_VALUE) {
                printf("Setting frame pts\n");
                Video->Packet.pts = Video->Packet.dts = FrameNumber;
            }

            Result = avcodec_send_packet(Video->CodecContext, &Video->Packet);
            if (Result != 0) {
                av_log(NULL, AV_LOG_ERROR, "Error sending packet\n");
                return Result;
            }

            Result = avcodec_receive_frame(Video->CodecContext, Video->Frame);
            if (Result != 0 && Result != AVERROR_EOF) {
                av_log(NULL, AV_LOG_ERROR, "Error receiving frame\n");
                return Result;
            }

            if (Result == 0) {
                GotFrame = 1;
            }

            if (GotFrame) {
                printf("%10"PRId64", %10"PRId64", %8"PRId64"\n",
                    Video->Frame->pts, Video->Frame->pkt_dts, Video->Frame->pkt_duration);
                printf("Uploading %s frame of %i x %i\n",
                    av_get_pix_fmt_name(Video->CodecContext->pix_fmt),
                    Video->CodecContext->width, Video->CodecContext->height);

                RenderFrame(Video->CodecContext, Video->Frame, Window, QuadProgram, Quad, YTex, UTex, VTex);

                FrameNumber++;
            }

            av_packet_unref(&Video->Packet);
            av_init_packet(&Video->Packet);
        }

    } while (!EndOfStream || GotFrame);


    return 0;
}

void FreeVideo(video* Video) {
    av_packet_unref(&Video->Packet);
    av_frame_free(&Video->Frame);
    avcodec_close(Video->CodecContext);
    avformat_close_input(&Video->FormatContext);
    avcodec_free_context(&Video->CodecContext);
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
    if (VideoDecodeExample(Video, Window) != 0) {
        return 1;
    }

    return 0;
}
