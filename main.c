#include <SDL.h>
#include <GL/glew.h>
#include <stdbool.h>
#include "shader.h"
#include "quad.h"
#include "texture.h"
#include "pa_ringbuffer.h"
#include <portaudio.h>

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

#define SAMPLE_RATE 44100
#define BLOCK_SIZE 128
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
} video;


void RenderFrame(AVFrame* Frame,
    int Width, int Height,
    SDL_Window* Window, GLuint QuadProgram, GLuint Quad,
    GLuint YTex, GLuint UTex, GLuint VTex)
{
    UpdateTexture(YTex, Width,     Height,     GL_RED, Frame->data[0], Frame->linesize[0]); // Y pixels
    UpdateTexture(UTex, Width*0.5, Height*0.5, GL_RED, Frame->data[1], Frame->linesize[1]); // U pixels
    UpdateTexture(VTex, Width*0.5, Height*0.5, GL_RED, Frame->data[2], Frame->linesize[2]); // V pixels

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
    stream* Stream)
{
    int Result = 0;

    Stream->Index = av_find_best_stream(FormatContext, MediaType, -1, -1, NULL, 0);
    if (Stream->Index < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't find stream in input file\n");
        return false;
    }
    printf("Assigned stream index %i\n", Stream->Index);

    AVCodecParameters* CodecParams = FormatContext->streams[Stream->Index]->codecpar;
    Stream->Codec = avcodec_find_decoder(CodecParams->codec_id);
    if (Stream->Codec == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Can't find decoder\n");
        return false;
    }

    Stream->CodecContext = avcodec_alloc_context3(Stream->Codec);
    if (Stream->CodecContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate decoder context\n");
        // AVERROR(ENOMEM);
        return false;
    }

    Result = avcodec_parameters_to_context(Stream->CodecContext, CodecParams);
    if (Result) {
        av_log(NULL, AV_LOG_ERROR, "Can't copy decoder context\n");
        return false;
    }

    Result = avcodec_open2(Stream->CodecContext, Stream->Codec, NULL);
    if (Result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't open decoder\n");
        return false;
    }

    for (int FrameIndex = 0; FrameIndex < QUEUE_FRAMES; FrameIndex++) {
        Stream->FrameQueue[FrameIndex].Frame = av_frame_alloc();
        if (!Stream->FrameQueue[FrameIndex].Frame) {
            av_log(NULL, AV_LOG_ERROR, "Can't allocate frame\n");
            // return AVERROR(ENOMEM);
            return false;
        }

        // Prevent initial blank frames from being presented
        Stream->FrameQueue[FrameIndex].Presented = true;
    }

    Stream->Timebase = av_q2d(FormatContext->streams[Stream->Index]->time_base);

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
        &Video->AudioStream
        );

    bool FoundVideo = OpenCodec(AVMEDIA_TYPE_VIDEO,
        Video->FormatContext,
        &Video->VideoStream
        );
    printf("Video stream index: %i\n", Video->VideoStream.Index);
    printf("Audio stream index: %i\n", Video->AudioStream.Index);

    Video->Width  = Video->VideoStream.CodecContext->width;
    Video->Height = Video->VideoStream.CodecContext->height;

    return Video;
}

void DecodeNextFrame(video* Video) {

    int Result;

    av_init_packet(&Video->Packet);

    if (!Video->EndOfStream) {
        Result = av_read_frame(Video->FormatContext, &Video->Packet);
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
    stream* Stream        = NULL;
    if (StreamIndex == Video->AudioStream.Index) {
        Stream = &Video->AudioStream;
    } else if (StreamIndex == Video->VideoStream.Index) {
        Stream = &Video->VideoStream;
    } else {
        printf("Unknown stream index %i\n", StreamIndex);
        av_packet_unref(&Video->Packet);
        av_init_packet(&Video->Packet);
        return;
    }

    int WriteHead = Stream->WriteHead;
    AVCodec*        Codec        = Stream->Codec;
    AVCodecContext* CodecContext = Stream->CodecContext;
    queued_frame*   QFrame       = &Stream->FrameQueue[WriteHead];
    Stream->WriteHead            = (WriteHead + 1) % QUEUE_FRAMES;

    Result = avcodec_send_packet(CodecContext, &Video->Packet);
    if (Result != 0) {
        av_log(NULL, AV_LOG_ERROR, "Error sending packet\n");
        return;
    }

    Result = avcodec_receive_frame(CodecContext, QFrame->Frame);
    if (Result != 0 && Result != AVERROR_EOF) {
        av_log(NULL, AV_LOG_ERROR, "Error receiving frame\n");
        return;
    }
    QFrame->PTS = QFrame->Frame->pts * Stream->Timebase;
    QFrame->Presented = false;

    av_packet_unref(&Video->Packet);
    av_init_packet(&Video->Packet);
}

void FreeVideo(video* Video) {
    av_packet_unref(&Video->Packet);

    for (int FrameIndex = 0; FrameIndex < QUEUE_FRAMES; FrameIndex++) {
        av_frame_free(&Video->VideoStream.FrameQueue[FrameIndex].Frame);
        av_frame_free(&Video->AudioStream.FrameQueue[FrameIndex].Frame);
    }

    avcodec_close(Video->VideoStream.CodecContext);
    avcodec_close(Video->AudioStream.CodecContext);
    avformat_close_input(&Video->FormatContext);
    avcodec_free_context(&Video->VideoStream.CodecContext);
    avcodec_free_context(&Video->AudioStream.CodecContext);
}

#define AUDIO_QUEUE 16 // must be power of 2
typedef struct {
    float* Samples;
    int Length;
    int NextSampleIndex;
    int BlockID;
} audio_block;

typedef struct {
    PaUtilRingBuffer BlocksRingBuf;
    void*            BlocksRingBufStorage;
    int ReadBlockIndex;
    int WriteBlockIndex;
    audio_block Blocks[AUDIO_QUEUE];
    PaStream* Stream;
} audio_state;

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

void QueueAudioFrame(AVFrame* Frame, AVCodecContext* CodecContext, audio_state* AudioState) {
    int Length = av_samples_get_buffer_size(NULL,
        CodecContext->channels, Frame->nb_samples, CodecContext->sample_fmt, 0);

    float* Samples = malloc(Length);
    memcpy(Samples, Frame->data[0], Length);

    static int NextBlockID = 0;
    audio_block AudioBlock = {
        .BlockID         = NextBlockID++,
        .Samples         = Samples,
        .Length          = Frame->nb_samples,
        .NextSampleIndex = 0
    };
    PaUtil_WriteRingBuffer(&AudioState->BlocksRingBuf, &AudioBlock, 1);
}

int main(int argc, char const *argv[]) {
    av_register_all();

    audio_state* AudioState = StartAudio();

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
    // video* Video = OpenVideo("mario.mp4");

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

    const int StartMS = SDL_GetTicks();

    printf("av_get_sample_fmt_name %s\n", av_get_sample_fmt_name(Video->AudioStream.CodecContext->sample_fmt));
    // Enqueue the first frame
    DecodeNextFrame(Video);
    while (!Video->EndOfStream) {
        const double Now = (double)(SDL_GetTicks() - StartMS) / 1000.0;

        // FIXME: Pull along the audio/video ReadHeads until the PTS is roughly in sync

        queued_frame* NextVideoFrame = &Video->VideoStream.FrameQueue[Video->VideoStream.ReadHead];
        if (!NextVideoFrame->Presented && Now >= NextVideoFrame->PTS) {
            printf("Reading Video frame %i\n", Video->VideoStream.ReadHead);
            RenderFrame(NextVideoFrame->Frame,
                Video->Width, Video->Height,
                Window, QuadProgram, Quad, YTex, UTex, VTex);
            Video->VideoStream.ReadHead = (Video->VideoStream.ReadHead + 1) % QUEUE_FRAMES;
            NextVideoFrame->Presented = true;
            av_frame_unref(NextVideoFrame->Frame);
        }

        queued_frame* NextAudioFrame = &Video->AudioStream.FrameQueue[Video->AudioStream.ReadHead];
        // printf("A: %f\n", NextAudioFrame->PTS);
        // printf("V: %f\n", NextVideoFrame->PTS);
        if (!NextAudioFrame->Presented && Now >= NextAudioFrame->PTS) {
            printf("Reading Audio frame %i\n", Video->AudioStream.ReadHead);
            QueueAudioFrame(NextAudioFrame->Frame, Video->AudioStream.CodecContext, AudioState);

            Video->AudioStream.ReadHead = (Video->AudioStream.ReadHead + 1) % QUEUE_FRAMES;
            NextAudioFrame->Presented = true;
            av_frame_unref(NextAudioFrame->Frame);
        }

        if (NextAudioFrame->Presented || NextVideoFrame->Presented) {
            DecodeNextFrame(Video);
        }
    }

    FreeVideo(Video);
    // printf("%10"PRId64", %10"PRId64", %8"PRId64"\n",
    //     Video->Frame->pts, Video->Frame->pkt_dts, Video->Frame->pkt_duration);
    // printf("Uploading %s frame of %i x %i\n",
    //     av_get_pix_fmt_name(Video->VideoCodecContext->pix_fmt),
    //     Video->Width, Video->Height);

    return 0;
}
