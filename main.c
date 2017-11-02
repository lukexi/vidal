#include <SDL.h>
#include <GL/glew.h>
#include "shader.h"
#include "quad.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define INBUF_SIZE 4096

static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt)
{
    char buf[1024];
    int ret;

    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return;
        }
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }

        printf("saving frame %3d\n", dec_ctx->frame_number);
        fflush(stdout);

        /* the picture is allocated by the decoder. no need to
           free it */
        snprintf(buf, sizeof(buf), "%d", dec_ctx->frame_number);

        printf("Handle a frame with wrap: %i size: %i x %i\n",
            frame->linesize[0],
            frame->width, frame->height);
        // pgm_save(frame->data[0], frame->linesize[0],
        //          frame->width, frame->height, buf);
    }
}

int DoFFMPEGStuff()
{
    const char *filename;
    const AVCodec *codec;
    AVCodecParserContext *parser;
    FILE *f;
    AVFrame *frame;
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    uint8_t *data;
    size_t   data_size;
    int ret;
    AVPacket *pkt;

    filename    = "mario.mp4";
    // filename    = "pinball.mov";

    av_register_all();

    pkt = av_packet_alloc();
    if (!pkt) {
        printf("Couldn't allocate packet.\n");
        exit(1);
    }

    /* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    /* find the video decoder */

    AVFormatContext *formatContext = avformat_alloc_context();

    // Open video file
    if (avformat_open_input(&formatContext, filename, NULL, NULL) != 0) {
        printf("Couldn't open file\n");
        exit(1); //
    }

    // Retrieve stream information
    if (avformat_find_stream_info(formatContext, NULL) < 0) {
        fprintf(stderr, "Codec not found\n");
        exit(1); // Couldn't find stream information
    }
    // Dump information about file onto standard error
    av_dump_format(formatContext, 0, filename, 0);


    int videoStream;
    AVCodecParameters *CodecParams = NULL;

    CodecParams = formatContext->streams[videoStream]->codecpar;

    codec = avcodec_find_decoder(CodecParams->codec_id);
    if (!codec) {
        av_log(NULL, AV_LOG_ERROR, "Can't find decoder\n");
        return -1;
    }

    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate decoder context\n");
        return AVERROR(ENOMEM);
    }

    // Copy video parameters to our CodecContext
    ret = avcodec_parameters_to_context(codecContext, CodecParams);
    if (ret) {
        av_log(NULL, AV_LOG_ERROR, "Can't copy decoder context\n");
        return ret;
    }

    // Find the first video stream
    videoStream = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

    if (videoStream == -1) {
        printf("Couldn't find a video stream\n");
        return -1;
    }
    printf("Found a video stream.\n");

    // Open codec

    parser = av_parser_init(codec->id);
    if (!parser) {
        fprintf(stderr, "parser not found\n");
        exit(1);
    }
    printf("Initialized parser.\n");

    /* For some codecs, such as msmpeg4 and mpeg4, width and height
       MUST be initialized there because this information is not
       available in the bitstream. */

    /* open it */
    if (avcodec_open2(codecContext, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }
    printf("Opened codec.\n");

    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    printf("Allocated frame.\n");

    while (!feof(f)) {
        printf("Reading a chunk of %i\n", INBUF_SIZE);
        /* read raw data from the input file */
        data_size = fread(inbuf, 1, INBUF_SIZE, f);
        if (!data_size)
            break;

        /* use the parser to split the data into frames */
        data = inbuf;
        while (data_size > 0) {
            ret = av_parser_parse2(parser, codecContext, &pkt->data, &pkt->size,
                                   data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (ret < 0) {
                fprintf(stderr, "Error while parsing\n");
                exit(1);
            }
            data      += ret;
            data_size -= ret;
            printf("Packet size is now %i\n", pkt->size);
            if (pkt->size)
                decode(codecContext, frame, pkt);
        }
    }
    printf("File ended.\n");

    /* flush the decoder */
    decode(codecContext, frame, NULL);

    fclose(f);

    av_parser_close(parser);
    avcodec_free_context(&codecContext);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return 0;
}

#define NUM_QUADS 30

int main(int argc, char const *argv[]) {
    int result = DoFFMPEGStuff();
    printf("Result: %i\n", result);
    return 0;


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

    GLuint QuadProgram = CreateVertFragProgramFromPath(
        "quad.vert",
        "quad.frag");

    GLuint Quads[NUM_QUADS];
    const float BoxSize = 1.0/NUM_QUADS;
    for (int QuadIndex = 0; QuadIndex < NUM_QUADS; QuadIndex++) {

        const float X0 = BoxSize * (QuadIndex + 0) * 2 - 1;
        const float X1 = BoxSize * (QuadIndex + 1) * 2 - 1;
        const float Y0 = 0 - BoxSize;
        const float Y1 = 0 + BoxSize;

        const float Vertices[8] = {
            X0, Y0,  // Left Top
            X0, Y1,  // Left Bottom
            X1, Y0,  // Right Top
            X1, Y1   // Right Bottom
        };
        Quads[QuadIndex] = CreateQuad(Vertices);
    }

    glUseProgram(QuadProgram);
    GLint VarLoc = glGetUniformLocation(QuadProgram, "VAR");
    GLint TimeLoc = glGetUniformLocation(QuadProgram, "uTime");

    while (1) {
        SDL_Event Event;
        while (SDL_PollEvent(&Event)) {
            if (Event.type == SDL_QUIT) exit(0);
        }

        glClearColor(0, 0.1, 0.1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glUniform1f(TimeLoc, SDL_GetTicks());

        for (int QuadIndex = 0; QuadIndex < NUM_QUADS; QuadIndex++) {
            glUniform1f(VarLoc, (float)QuadIndex/(float)NUM_QUADS);
            glBindVertexArray(Quads[QuadIndex]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        SDL_GL_SwapWindow(Window);
    }

    return 0;
}
