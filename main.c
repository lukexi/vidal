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

static int VideoDecodeExample(const char *InputFilename, SDL_Window* Window)
{
    AVCodec *Codec = NULL;
    AVCodecContext *CodecContext= NULL;
    AVCodecParameters *CodecParams = NULL;
    AVFrame *Frame = NULL;
    uint8_t *ByteBuffer = NULL;
    AVPacket Packet;
    AVFormatContext *FormatContext = NULL;
    int NumberOfWrittenBytes;
    int VideoStream;
    int GotFrame = 0;
    int ByteBufferSize;
    int FrameNumber = 0;
    int Result;
    int EndOfStream = 0;

    Result = avformat_open_input(&FormatContext, InputFilename, NULL, NULL);
    if (Result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't open file\n");
        return Result;
    }

    Result = avformat_find_stream_info(FormatContext, NULL);
    if (Result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't get stream info\n");
        return Result;
    }

    VideoStream = av_find_best_stream(FormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (VideoStream < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't find video stream in input file\n");
        return -1;
    }

    CodecParams = FormatContext->streams[VideoStream]->codecpar;

    Codec = avcodec_find_decoder(CodecParams->codec_id);
    if (!Codec) {
        av_log(NULL, AV_LOG_ERROR, "Can't find decoder\n");
        return -1;
    }

    CodecContext = avcodec_alloc_context3(Codec);
    if (!CodecContext) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate decoder context\n");
        return AVERROR(ENOMEM);
    }

    Result = avcodec_parameters_to_context(CodecContext, CodecParams);
    if (Result) {
        av_log(NULL, AV_LOG_ERROR, "Can't copy decoder context\n");
        return Result;
    }

    Result = avcodec_open2(CodecContext, Codec, NULL);
    if (Result < 0) {
        av_log(CodecContext, AV_LOG_ERROR, "Can't open decoder\n");
        return Result;
    }

    Frame = av_frame_alloc();
    if (!Frame) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate frame\n");
        return AVERROR(ENOMEM);
    }

    ByteBufferSize = av_image_get_buffer_size(CodecContext->pix_fmt, CodecContext->width, CodecContext->height, 16);
    ByteBuffer = av_malloc(ByteBufferSize);
    if (!ByteBuffer) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate buffer\n");
        return AVERROR(ENOMEM);
    }

    GLuint QuadProgram = CreateVertFragProgramFromPath(
        "quad.vert",
        "quad.frag");
    glUseProgram(QuadProgram);

    GLuint YTex = CreateTexture(CodecContext->width,     CodecContext->height,     1);
    GLuint UTex = CreateTexture(CodecContext->width*0.5, CodecContext->height*0.5, 1);
    GLuint VTex = CreateTexture(CodecContext->width*0.5, CodecContext->height*0.5, 1);

    float Verts[8] = {
        -1, -1, // Left Top
        -1, 1,  // Left Bottom
        1, -1,  // Right Top
        1, 1    // Right Bottom
    };
    GLuint Quad = CreateQuad(Verts);

    printf("#tb %d: %d/%d\n", VideoStream, FormatContext->streams[VideoStream]->time_base.num, FormatContext->streams[VideoStream]->time_base.den);
    FrameNumber = 0;
    av_init_packet(&Packet);
    do {
        if (!EndOfStream) {
            if (av_read_frame(FormatContext, &Packet) < 0) {
                EndOfStream = 1;
            }
        }
        if (EndOfStream) {
            Packet.data = NULL;
            Packet.size = 0;
        }
        if (Packet.stream_index == VideoStream || EndOfStream) {
            GotFrame = 0;

            if (Packet.pts == AV_NOPTS_VALUE) {
                Packet.pts = Packet.dts = FrameNumber;
            }

            Result = avcodec_decode_video2(CodecContext, Frame, &GotFrame, &Packet);
            if (Result < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error decoding frame\n");
                return Result;
            }

            if (GotFrame) {
                NumberOfWrittenBytes = av_image_copy_to_buffer(
                    ByteBuffer, ByteBufferSize,
                    (const uint8_t* const *)Frame->data,
                    (const int*)Frame->linesize,
                    CodecContext->pix_fmt,
                    CodecContext->width,
                    CodecContext->height,
                    1);
                if (NumberOfWrittenBytes < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Can't copy image to buffer\n");
                    return NumberOfWrittenBytes;
                }
                printf("%d, %10"PRId64", %10"PRId64", %8"PRId64", %8d, 0x%08lx\n", VideoStream,
                        Frame->pts, Frame->pkt_dts, Frame->pkt_duration,
                        NumberOfWrittenBytes, av_adler32_update(0, (const uint8_t*)ByteBuffer, NumberOfWrittenBytes));
                printf("%s\n", av_get_pix_fmt_name(CodecContext->pix_fmt));

                printf("Uploading %i %i %i bytes\n", ByteBufferSize, CodecContext->width, CodecContext->height);
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
                printf("i: %i\n", i);

                glBindVertexArray(Quad);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                SDL_GL_SwapWindow(Window);
                SDL_Event Event;
                while (SDL_PollEvent(&Event)) {
                    if (Event.type == SDL_QUIT) exit(0);
                }
                FrameNumber++;
            }
            av_packet_unref(&Packet);
            av_init_packet(&Packet);
        }

    } while (!EndOfStream || GotFrame);

    av_packet_unref(&Packet);
    av_frame_free(&Frame);
    avcodec_close(CodecContext);
    avformat_close_input(&FormatContext);
    avcodec_free_context(&CodecContext);
    av_freep(&ByteBuffer);
    return 0;
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

    if (VideoDecodeExample("pinball.mov", Window) != 0) {
        return 1;
    }

    return 0;
}
