#include "video.h"
#include "utils.h"
#include "texture.h"
#define NANOVG_GL3
#include "nanovg_gl.h"
#include "video-audio.h"
#include <pthread.h>
#include <assert.h>

#define FRAME_BUFFER_SIZE 32 // Must be power of 2
#define HALF_FRAME_BUFFER_SIZE (FRAME_BUFFER_SIZE / 2)

void DecodeVideo(video* Video);

double GetFramePTS(AVFrame* Frame, stream* Stream);
double GetVideoFrameDuration(video* Video);
double GetVideoTime(video* Video);
void SeekVideo(video* Video, double Timestamp);

void DecodeNextFrame(video* Video);

double GetTimeInSeconds() {
    return (double)GetTimeInMicros() / 1000000.0;
}

void OpenCodec(
    enum AVMediaType MediaType,
    AVFormatContext* FormatContext,
    stream* Stream)
{
    int Result = 0;

    Stream->Index = av_find_best_stream(FormatContext, MediaType, -1, -1, NULL, 0);
    if (Stream->Index < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't find stream in input file\n");
        return;
    }

    AVCodecParameters* CodecParams = FormatContext->streams[Stream->Index]->codecpar;
    Stream->Codec = avcodec_find_decoder(CodecParams->codec_id);
    if (Stream->Codec == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Can't find decoder\n");
        return;
    }

    Stream->CodecContext = avcodec_alloc_context3(Stream->Codec);
    if (Stream->CodecContext == NULL) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate decoder context\n");
        // AVERROR(ENOMEM);
        return;
    }

    Result = avcodec_parameters_to_context(Stream->CodecContext, CodecParams);
    if (Result) {
        av_log(NULL, AV_LOG_ERROR, "Can't copy decoder context\n");
        return;
    }

    Result = avcodec_open2(Stream->CodecContext, Stream->Codec, NULL);
    if (Result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't open decoder\n");
        return;
    }

    Stream->Timebase = av_q2d(FormatContext->streams[Stream->Index]->time_base);

    Stream->Valid = true;
}

void* DecodeThreadMain(void* Arg) {
    video* Video = Arg;

    while (!Video->StopDecodeThread) {
        DecodeVideo(Video);
    }
    return NULL;
}

video* OpenVideo(const char* InputFilename, NVGcontext* NVG, audio_state* AudioState) {
    video* Video = calloc(1, sizeof(video));

    Video->AudioState = AudioState;
    Video->NVG = NVG;

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

    OpenCodec(AVMEDIA_TYPE_AUDIO,
        Video->FormatContext,
        &Video->AudioStream
        );

    OpenCodec(AVMEDIA_TYPE_VIDEO,
        Video->FormatContext,
        &Video->VideoStream
        );

    if (!Video->VideoStream.Valid && !Video->AudioStream.Valid) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't find an audio or video stream\n");
        free(Video);
        return NULL;
    }

    if (Video->VideoStream.Valid) {
        Video->Width  = Video->VideoStream.CodecContext->width;
        Video->Height = Video->VideoStream.CodecContext->height;

        Video->ColorConvertContext = sws_getContext(
                Video->Width, Video->Height, Video->VideoStream.CodecContext->pix_fmt,
                Video->Width, Video->Height, AV_PIX_FMT_RGB24,
                0, NULL, NULL, NULL);
        Video->ColorConvertBufferSize = 3*Video->Width*Video->Height;
        Video->ColorConvertBuffer = malloc(Video->ColorConvertBufferSize);

        Video->Texture = CreateTexture(Video->Width, Video->Height, 3);
        Video->NVGImage = nvglCreateImageFromHandleGL3(
                    NVG,
                    Video->Texture,
                    Video->Width,
                    Video->Height,
                    NVG_IMAGE_NODELETE // This texture ID isn't owned by NVG,
                                       // don't delete it when deleting the NVG Image!
                );
    }

    CreateRingBuffer(&Video->VideoStream.Buffer, sizeof(AVFrame*), FRAME_BUFFER_SIZE);
    CreateRingBuffer(&Video->AudioStream.Buffer, sizeof(AVFrame*), FRAME_BUFFER_SIZE);

    Video->StartTime = GetTimeInSeconds();

    // printf("Opened %ix%i video with video format %s audio format %s\n",
    //     Video->Width, Video->Height,
    //     av_get_pix_fmt_name(Video->VideoStream.CodecContext->pix_fmt),
    //     av_get_sample_fmt_name(Video->AudioStream.CodecContext->sample_fmt)
    //     );

    Video->AudioChannel = GetNextChannel(AudioState);

    int ResultCode = pthread_create(&Video->DecodeThread, NULL, DecodeThreadMain, Video);
    assert(!ResultCode);

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
    stream* Stream = NULL;
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

    AVFrame* Frame = av_frame_alloc();

    Result = avcodec_receive_frame(CodecContext, Frame);
    if (Result != 0 && Result != AVERROR_EOF && Result != AVERROR(EAGAIN)) {
        av_log(NULL, AV_LOG_ERROR, "Error receiving frame\n");
        return;
    }

    if (Result == 0) {
        if (GetRingBufferWriteAvailable(&Stream->Buffer) > 0) {
            WriteRingBuffer(&Stream->Buffer, &Frame, 1);
        } else {
            // Otherwise, we just drop the frame
            av_frame_free(&Frame);
        }
    }

    av_packet_unref(&Video->Packet);
    av_init_packet(&Video->Packet);

    if (Result == AVERROR(EAGAIN)) {
        printf("Buffering...\n");
        DecodeNextFrame(Video);
    }
}


void UploadVideoFrame(video* Video, AVFrame* Frame) {
    // Use https://www.ffmpeg.org/ffmpeg-scaler.html
    // to convert from YUV420P to packed RGB24
    uint8_t* OutputData[1] = { Video->ColorConvertBuffer }; // RGB24 have one plane
    int OutputLineSize[1] = { 3 * Video->Width }; // RGB stride

    int Result = sws_scale(Video->ColorConvertContext,
        (const uint8_t *const *)Frame->data,
        Frame->linesize,
        0,             // Begin slice
        Video->Height, // Num slices
        OutputData,
        OutputLineSize);
    (void)Result;

    UpdateTexture(Video->Texture, Video->Width, Video->Height, GL_RGB, Video->ColorConvertBuffer);
}


void GetCurrentFrame(const char* Label, video* Video, stream* Stream, AVFrame** Frame) {
    const double Now = GetVideoTime(Video);

    bool CaughtUp = false;
    while ((!CaughtUp) &&
            GetRingBufferReadAvailable(&Stream->Buffer) >= 2)
    {
        AVFrame* Frames[2];
        PeekRingBuffer(&Stream->Buffer, Frames, 2);

        AVFrame* CurrFrame = Frames[0];
        AVFrame* NextFrame = Frames[1];

        const double CurrPTS = GetFramePTS(CurrFrame, Stream);
        const double NextPTS = GetFramePTS(NextFrame, Stream);

        if (CurrPTS <= Now &&
            NextPTS >  Now) {
            // printf("%s NOW: %f FRAME: %f \n", Label, Now, CurrPTS);
            *Frame = CurrFrame;
            AdvanceRingBufferReadIndex(&Stream->Buffer, 1);
            CaughtUp = true;
        } else if (CurrPTS < Now && NextPTS < Now) {
            // Drop the frame
            AdvanceRingBufferReadIndex(&Stream->Buffer, 1);
            printf("DROPPING A FRAME\n");
            av_frame_free(&CurrFrame);
        } else if (CurrPTS > Now && NextPTS > Now) {
            // Not time for this frame yet
            CaughtUp = true;
        }
    }
}


void TickVideo(video* Video) {

    AVFrame* VideoFrame = NULL;
    GetCurrentFrame("V", Video, &Video->VideoStream, &VideoFrame);
    if (VideoFrame) {
        UploadVideoFrame(Video, VideoFrame);
        av_frame_free(&VideoFrame);
    }
}

ring_buffer_size_t GetAudioChannelCapacity(video* Video) {
    audio_state* AudioState = Video->AudioState;
    return GetRingBufferWriteAvailable(&AudioState->Channels[Video->AudioChannel].BlocksIn);
}

void QueueAudioFrame(AVFrame* Frame, video* Video) {
    audio_state* AudioState = Video->AudioState;
    AVCodecContext* CodecContext = Video->AudioStream.CodecContext;
    int Length = av_samples_get_buffer_size(NULL,
        CodecContext->channels, Frame->nb_samples, CodecContext->sample_fmt, 0);

    float* Samples = malloc(Length);
    memcpy(Samples, Frame->data[0], Length);

    // FIXME: Use:
    // https://www.ffmpeg.org/ffmpeg-resampler.html
    // to convert audio to interleaved stereo

    audio_block AudioBlock = {
        .Samples         = Samples,
        .Length          = Frame->nb_samples,
        .NextSampleIndex = 0
    };

    WriteRingBuffer(&AudioState->Channels[Video->AudioChannel].BlocksIn, &AudioBlock, 1);
}

double GetVideoTime(video* Video) {
    return GetTimeInSeconds() - Video->StartTime;
}



// Decodes video so long as there is buffer space available.
void DecodeVideo(video* Video) {
    if (!Video) {
        return;
    }

    ring_buffer_size_t NumBufferedVideoFrames = GetRingBufferReadAvailable(&Video->VideoStream.Buffer);
    ring_buffer_size_t NumBufferedAudioFrames = GetRingBufferReadAvailable(&Video->AudioStream.Buffer);

    // printf("Number of buffered audio frames: %i\n", NumBufferedAudioFrames);
    // printf("Number of buffered video frames: %i\n", NumBufferedVideoFrames);
    if (
        NumBufferedAudioFrames < HALF_FRAME_BUFFER_SIZE
        // NumBufferedVideoFrames < HALF_FRAME_BUFFER_SIZE
        ) {
        // printf("DECODING A BUNCHA SHIT\n");
        DecodeNextFrame(Video);
    }


    // Enqueue audio
    AVFrame* AudioFrame = NULL;
    GetCurrentFrame("A", Video, &Video->AudioStream, &AudioFrame);
    if (AudioFrame) {
        QueueAudioFrame(AudioFrame, Video);
        av_frame_free(&AudioFrame);
    }

    if (Video->EndOfStream) {
        Video->EndOfStream = 0;
        printf("SEEKING\n");
        SeekVideo(Video, 0);
    }
}

double GetFramePTS(AVFrame* Frame, stream* Stream) {
    return Frame->pts * Stream->Timebase;
}

double GetVideoFrameDuration(video* Video) {
    return Video->VideoStream.Timebase * 1000;
}

void FlushStream(stream* Stream) {
    if (!Stream->Valid) return;

    avcodec_flush_buffers(Stream->CodecContext);

    ring_buffer_size_t FramesCount = GetRingBufferReadAvailable(&Stream->Buffer);

    for (int I = 0; I < FramesCount; I++) {
        AVFrame* Frame = NULL;
        ReadRingBuffer(&Stream->Buffer, &Frame, 1);
        av_frame_free(&Frame);
    }
}

void SeekVideo(video* Video, double Timestamp) {
    if (!Video) return;

    if (Video->VideoStream.Valid) {
        int64_t VideoPTS = Timestamp / Video->VideoStream.Timebase;
        av_seek_frame(Video->FormatContext, Video->VideoStream.Index,
            VideoPTS, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
        FlushStream(&Video->VideoStream);
    }

    if (Video->AudioStream.Valid) {
        int64_t AudioPTS = Timestamp / Video->AudioStream.Timebase;
        av_seek_frame(Video->FormatContext, Video->AudioStream.Index,
            AudioPTS, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
        FlushStream(&Video->AudioStream);
    }

    Video->StartTime = GetTimeInSeconds() - Timestamp;
}

void FreeVideo(video* Video) {
    if (!Video) return;

    Video->StopDecodeThread = true;
    pthread_join(Video->DecodeThread, NULL);

    av_packet_unref(&Video->Packet);

    if (Video->VideoStream.Valid) {
        glDeleteTextures(1, &Video->Texture);
        nvgDeleteImage(Video->NVG, Video->NVGImage);
        sws_freeContext(Video->ColorConvertContext);
        free(Video->ColorConvertBuffer);

        FlushStream(&Video->VideoStream);

        avcodec_close(Video->VideoStream.CodecContext);
        avcodec_free_context(&Video->VideoStream.CodecContext);
    }

    if (Video->AudioStream.Valid) {
        FlushStream(&Video->AudioStream);

        avcodec_close(Video->AudioStream.CodecContext);
        avcodec_free_context(&Video->AudioStream.CodecContext);
    }

    avformat_close_input(&Video->FormatContext);

    FreeRingBuffer(&Video->VideoStream.Buffer);
    FreeRingBuffer(&Video->AudioStream.Buffer);

    free(Video);
}
