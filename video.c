#include "video.h"
#include "utils.h"
#include "texture.h"
#define NANOVG_GL3
#include "nanovg_gl.h"
#include "video-audio.h"

void DecodeNextFrame(video* Video);

double GetTimeInSeconds() {
    return (double)GetTimeInMicros() / 1000000.0;
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

video* OpenVideo(const char* InputFilename, NVGcontext* NVG, audio_state* AudioState) {
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

    Video->StartTime = GetTimeInSeconds();

    // Load the first frame into the Video structure
    DecodeNextFrame(Video);

    printf("Opened %ix%i video with video format %s audio format %s\n",
        Video->Width, Video->Height,
        av_get_pix_fmt_name(Video->VideoStream.CodecContext->pix_fmt),
        av_get_sample_fmt_name(Video->AudioStream.CodecContext->sample_fmt)
        );

    Video->AudioChannel = GetNextChannel(AudioState);

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


void UploadVideoFrame(video* Video, AVFrame* Frame) {
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

    UpdateTexture(Video->Texture, Video->Width, Video->Height, GL_RGB, Video->ColorConvertBuffer);
}

void QueueAudioFrame(AVFrame* Frame, video* Video, audio_state* AudioState) {
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

    PaUtil_WriteRingBuffer(&AudioState->Channels[Video->AudioChannel].BlocksRingBuf, &AudioBlock, 1);
}

void MarkFramePresented(queued_frame* QFrame) {
    QFrame->Presented = true;
    av_frame_unref(QFrame->Frame);
}

bool FrameIsReady(queued_frame* Frame, double Now) {
    return !Frame->Presented && Now >= Frame->PTS;
}

// Checks if the decoder has outpaced us, and drops up to
// MAX_FRAME_DROP frames if so.
queued_frame* GetNextFrame(stream* Stream, double Now) {

    const int MAX_FRAME_DROP = QUEUE_FRAMES; // make this equal to QUEUE_FRAMES?
    for (int Readahead = 0; Readahead < MAX_FRAME_DROP; Readahead++) {
        int CurrHead = (Stream->ReadHead + Readahead)     % QUEUE_FRAMES;
        int NextHead = (Stream->ReadHead + Readahead + 1) % QUEUE_FRAMES;
        queued_frame* CurrFrame = &Stream->FrameQueue[CurrHead];
        queued_frame* NextFrame = &Stream->FrameQueue[NextHead];

        // If the current frame and the next frame are ready,
        // drop the current frame.
        if (FrameIsReady(CurrFrame, Now) && FrameIsReady(NextFrame, Now)) {
            // printf("DROPPING A FRAME\n");
            MarkFramePresented(CurrFrame);
        }
        // If the current frame is ready and the next frame is not,
        // return the current frame.
        else if (FrameIsReady(CurrFrame, Now)) {
            Stream->ReadHead = NextHead;
            return CurrFrame;
        }
    }
    return NULL;
}

bool TickVideo(video* Video, audio_state* AudioState) {

    const double Now = GetTimeInSeconds() - Video->StartTime;

    // FIXME: Pull along the audio/video ReadHeads until the PTS is roughly in sync
    queued_frame* VideoFrame = GetNextFrame(&Video->VideoStream, Now);
    if (VideoFrame) {
        UploadVideoFrame(Video, VideoFrame->Frame);
        MarkFramePresented(VideoFrame);
    }

    queued_frame* AudioFrame = GetNextFrame(&Video->AudioStream, Now);
    if (AudioFrame) {
        QueueAudioFrame(AudioFrame->Frame, Video, AudioState);
        MarkFramePresented(AudioFrame);
    }

    if (VideoFrame) {
        // If Now has gotten significantly ahead of the video,
        // buffer up some frames
        double Ahead = (Now - VideoFrame->PTS);
        if (Ahead > 3.0/60.0) {
            // printf("BUFFERING\n");
            // printf("NOW: %f\n", Now);
            // printf("PTS: %f\n", VideoFrame->PTS);
            // printf("AHEAD BY %f\n", Ahead);
            for (int i = 0; i < 10; i++) {
                DecodeNextFrame(Video);
            }
        }
    }
    if (AudioFrame || VideoFrame) {
        DecodeNextFrame(Video);
    }

    if (Video->EndOfStream) {
        Video->EndOfStream = 0;
        SeekVideo(Video, 0);
    }



    return AudioFrame || VideoFrame;
}

void FlushStream(stream* Stream) {
    avcodec_flush_buffers(Stream->CodecContext);
    Stream->ReadHead  = 0;
    Stream->WriteHead = 0;
    for (int FrameIndex = 0; FrameIndex < QUEUE_FRAMES; FrameIndex++) {
        MarkFramePresented(&Stream->FrameQueue[FrameIndex]);
    }
}

void SeekVideo(video* Video, double Timestamp) {
    int64_t VideoPTS = Timestamp / Video->VideoStream.Timebase;
    av_seek_frame(Video->FormatContext, Video->VideoStream.Index,
        VideoPTS, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);

    int64_t AudioPTS = Timestamp / Video->AudioStream.Timebase;
    av_seek_frame(Video->FormatContext, Video->AudioStream.Index,
        AudioPTS, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);

    Video->StartTime = GetTimeInSeconds() - Timestamp;

    FlushStream(&Video->VideoStream);
    FlushStream(&Video->AudioStream);

    DecodeNextFrame(Video);
}

void FreeVideo(video* Video, NVGcontext* NVG) {
    glDeleteTextures(1, &Video->Texture);

    nvgDeleteImage(NVG, Video->NVGImage);

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
