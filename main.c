#include <SDL.h>
#include <GL/glew.h>
#include <stdbool.h>
#include "shader.h"
#include "quad.h"
#include "texture.h"
#include "pa_ringbuffer.h"
#include <portaudio.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

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

    struct SwsContext* ColorConvertContext;
    size_t ColorConvertBufferSize;
    uint8_t* ColorConvertBuffer;

    double StartTime;
} video;


void RenderFrame(video* Video, AVFrame* Frame,
    SDL_Window* Window, GLuint QuadProgram, GLuint Quad,
    GLuint Tex)
{
    // Use https://www.ffmpeg.org/ffmpeg-scaler.html
    // to convert from YUV420P to packed RGB24
    uint8_t* OutputData[1] = { Video->ColorConvertBuffer }; // RGB24 have one plane
    int OutputLineSize[1] = { 3 * Video->Width }; // RGB stride

    int Result = sws_scale(Video->ColorConvertContext,
        (const uint8_t *const *)Frame->data,
        Frame->linesize,
        0,      // Begin slice
        Video->Height, // Num slices
        OutputData,
        OutputLineSize);
    (void)Result;

    UpdateTexture(Tex, Video->Width, Video->Height, GL_RGB, Video->ColorConvertBuffer);

    glClearColor(0, 0.1, 0.1, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform1i(glGetUniformLocation(QuadProgram, "uTex"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, Tex);

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

    AVCodecContext* CodecContext = Stream->CodecContext;

    Result = avcodec_send_packet(CodecContext, &Video->Packet);
    if (Result != 0) {
        av_log(NULL, AV_LOG_ERROR, "Error sending packet\n");
        return;
    }

    int WriteHead = Stream->WriteHead;
    queued_frame*   QFrame       = &Stream->FrameQueue[WriteHead];

    Result = avcodec_receive_frame(CodecContext, QFrame->Frame);
    if (Result != 0 && Result != AVERROR_EOF && Result != AVERROR(EAGAIN)) {
        av_log(NULL, AV_LOG_ERROR, "Error receiving frame\n");
        return;
    }

    if (Result == 0) {
        Stream->WriteHead = (WriteHead + 1) % QUEUE_FRAMES;
        QFrame->PTS = QFrame->Frame->pts * Stream->Timebase;
        QFrame->Presented = false;
    }

    av_packet_unref(&Video->Packet);
    av_init_packet(&Video->Packet);

    if (Result == AVERROR(EAGAIN)) {
        printf("Buffering...\n");
        DecodeNextFrame(Video);
    }
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
    (void)FoundAudio;
    (void)FoundVideo;
    printf("Video stream index: %i\n", Video->VideoStream.Index);
    printf("Audio stream index: %i\n", Video->AudioStream.Index);

    Video->Width  = Video->VideoStream.CodecContext->width;
    Video->Height = Video->VideoStream.CodecContext->height;

    Video->ColorConvertContext = sws_getContext(
            Video->Width, Video->Height, Video->VideoStream.CodecContext->pix_fmt,
            Video->Width, Video->Height, AV_PIX_FMT_RGB24,
            0, NULL, NULL, NULL);
    Video->ColorConvertBufferSize = 3*Video->Width*Video->Height;
    Video->ColorConvertBuffer = malloc(Video->ColorConvertBufferSize);

    Video->StartTime = ((double)SDL_GetTicks()/1000.0);

    // Load the first frame into the Video structure
    DecodeNextFrame(Video);

    printf("Opened %ix%i video with video format %s audio format %s\n",
        Video->Width, Video->Height,
        av_get_pix_fmt_name(Video->VideoStream.CodecContext->pix_fmt),
        av_get_sample_fmt_name(Video->AudioStream.CodecContext->sample_fmt)
        );

    return Video;
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

    sws_freeContext(Video->ColorConvertContext);

    free(Video->ColorConvertBuffer);
    free(Video);
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

    // FIXME: Use:
    // https://www.ffmpeg.org/ffmpeg-resampler.html
    // to convert audio to interleaved stereo

    static int NextBlockID = 0;
    audio_block AudioBlock = {
        .BlockID         = NextBlockID++,
        .Samples         = Samples,
        .Length          = Frame->nb_samples,
        .NextSampleIndex = 0
    };
    PaUtil_WriteRingBuffer(&AudioState->BlocksRingBuf, &AudioBlock, 1);
}

void SeekVideo(video* Video, double Timestamp) {
    int64_t VideoPTS = Timestamp / Video->VideoStream.Timebase;
    av_seek_frame(Video->FormatContext, Video->VideoStream.Index,
        VideoPTS, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);

    int64_t AudioPTS = Timestamp / Video->AudioStream.Timebase;
    av_seek_frame(Video->FormatContext, Video->AudioStream.Index,
        AudioPTS, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);

    Video->StartTime = ((double)SDL_GetTicks()/1000.0) - Timestamp;

    avcodec_flush_buffers(Video->VideoStream.CodecContext);
    avcodec_flush_buffers(Video->AudioStream.CodecContext);
    Video->VideoStream.ReadHead = 0;
    Video->VideoStream.WriteHead = 0;
    Video->AudioStream.ReadHead = 0;
    Video->AudioStream.WriteHead = 0;
    DecodeNextFrame(Video);
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

    // video* Video = OpenVideo("pinball.mov");
    // video* Video = OpenVideo("mario.mp4");
    // video* Video = OpenVideo("Martin_Luther_King_PBS_interview_with_Kenneth_B._Clark_1963.mp4");
    video* Video = OpenVideo("MartinLutherKing.mp4");

    GLuint QuadProgram = CreateVertFragProgramFromPath(
        "quad.vert",
        "quad.frag");
    glUseProgram(QuadProgram);

    GLuint Tex = CreateTexture(Video->Width, Video->Height, 3);

    float Verts[8] = {
        -1, -1, // Left Top
        -1, 1,  // Left Bottom
        1, -1,  // Right Top
        1, 1    // Right Bottom
    };
    GLuint Quad = CreateQuad(Verts);

    while (1) {
        const double Now = ((double)SDL_GetTicks() / 1000.0) - Video->StartTime;

        // FIXME: Pull along the audio/video ReadHeads until the PTS is roughly in sync

        queued_frame* NextVideoFrame = &Video->VideoStream.FrameQueue[Video->VideoStream.ReadHead];
        if (!NextVideoFrame->Presented && Now >= NextVideoFrame->PTS) {

            RenderFrame(Video, NextVideoFrame->Frame,
                Window, QuadProgram, Quad, Tex);

            Video->VideoStream.ReadHead = (Video->VideoStream.ReadHead + 1) % QUEUE_FRAMES;
            NextVideoFrame->Presented = true;
            av_frame_unref(NextVideoFrame->Frame);
        }

        queued_frame* NextAudioFrame = &Video->AudioStream.FrameQueue[Video->AudioStream.ReadHead];
        if (!NextAudioFrame->Presented && Now >= NextAudioFrame->PTS) {

            QueueAudioFrame(NextAudioFrame->Frame, Video->AudioStream.CodecContext, AudioState);

            Video->AudioStream.ReadHead = (Video->AudioStream.ReadHead + 1) % QUEUE_FRAMES;
            NextAudioFrame->Presented = true;
            av_frame_unref(NextAudioFrame->Frame);
        }

        if (NextAudioFrame->Presented || NextVideoFrame->Presented) {
            DecodeNextFrame(Video);
        }

        if (Video->EndOfStream) {
            Video->EndOfStream = 0;
            SeekVideo(Video, 0);
        }
    }

    FreeVideo(Video);



    return 0;
}
