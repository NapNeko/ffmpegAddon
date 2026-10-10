#include "audioPipeline.h"
#include "ffmpegCommon.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace
{
struct AudioFormat
{
    const char *muxer;
    AVCodecID codec;
    AVSampleFormat sampleFormat;
    int bitRate;
};

const std::map<std::string, AudioFormat> audioFormats = {
    {"mp3", {"mp3", AV_CODEC_ID_MP3, AV_SAMPLE_FMT_S16P, 128000}},
    {"amr", {"amr", AV_CODEC_ID_AMR_NB, AV_SAMPLE_FMT_S16, 12200}},
    {"wma", {"asf", AV_CODEC_ID_WMAV2, AV_SAMPLE_FMT_FLTP, 128000}},
    {"m4a", {"ipod", AV_CODEC_ID_AAC, AV_SAMPLE_FMT_FLTP, 128000}},
    {"spx", {"ogg", AV_CODEC_ID_SPEEX, AV_SAMPLE_FMT_S16, 24600}},
    {"ogg", {"ogg", AV_CODEC_ID_OPUS, AV_SAMPLE_FMT_FLTP, 48000}},
    {"wav", {"wav", AV_CODEC_ID_PCM_S16LE, AV_SAMPLE_FMT_S16, 0}},
    {"flac", {"flac", AV_CODEC_ID_FLAC, AV_SAMPLE_FMT_S16, 0}},
    {"pcm", {"s16le", AV_CODEC_ID_PCM_S16LE, AV_SAMPLE_FMT_S16, 0}},
    {"ntsilk", {"ntsilk_s16le", AV_CODEC_ID_NTSILK_S16LE, AV_SAMPLE_FMT_S16, 0}},
};

void CheckAudioResult(int result, const char *operation)
{
    if (result < 0)
    {
        char message[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, message, sizeof(message));
        throw std::runtime_error(std::string(operation) + ": " + message);
    }
}

class AudioPipeline
{
public:
    ~AudioPipeline()
    {
        av_frame_free(&decodedFrame_);
        av_frame_free(&resampledFrame_);
        av_frame_free(&encodedFrame_);
        av_packet_free(&inputPacket_);
        av_packet_free(&outputPacket_);
        av_audio_fifo_free(fifo_);
        swr_free(&resampler_);
        avcodec_free_context(&decoder_);
        avcodec_free_context(&encoder_);
        avformat_close_input(&input_);
        if (output_)
        {
            if (output_->pb)
                avio_closep(&output_->pb);
            avformat_free_context(output_);
        }
    }

    int Convert(const std::string &inputPath, const std::string &outputPath,
                const AudioFormat &format, int requestedSampleRate)
    {
        CheckAudioResult(avformat_open_input(&input_, inputPath.c_str(), nullptr, nullptr), "Failed to open input");
        CheckAudioResult(avformat_find_stream_info(input_, nullptr), "Failed to find stream info");
        const AVCodec *decoderCodec = nullptr;
        const int audioStream = av_find_best_stream(input_, AVMEDIA_TYPE_AUDIO, -1, -1, &decoderCodec, 0);
        CheckAudioResult(audioStream, "No decodable audio stream");
        decoder_ = avcodec_alloc_context3(decoderCodec);
        if (!decoder_)
            throw std::bad_alloc();
        CheckAudioResult(avcodec_parameters_to_context(decoder_, input_->streams[audioStream]->codecpar), "Failed to copy decoder parameters");
        CheckAudioResult(avcodec_open2(decoder_, decoderCodec, nullptr), "Failed to open decoder");

        const AVCodec *encoderCodec = avcodec_find_encoder(format.codec);
        if (!encoderCodec)
            throw std::runtime_error("Encoder not found");
        encoder_ = avcodec_alloc_context3(encoderCodec);
        if (!encoder_)
            throw std::bad_alloc();
        encoder_->sample_rate = SelectSampleRate(encoderCodec, requestedSampleRate, decoder_->sample_rate);
        encoder_->sample_fmt = format.sampleFormat;
        encoder_->time_base = {1, encoder_->sample_rate};
        av_channel_layout_default(&encoder_->ch_layout, 1);
        encoder_->bit_rate = format.bitRate;
        encoder_->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
        CheckAudioResult(avformat_alloc_output_context2(&output_, nullptr, format.muxer, outputPath.c_str()), "Failed to create output container");
        if (output_->oformat->flags & AVFMT_GLOBALHEADER)
            encoder_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        CheckAudioResult(avcodec_open2(encoder_, encoderCodec, nullptr), "Failed to open encoder");

        outputStream_ = avformat_new_stream(output_, nullptr);
        if (!outputStream_)
            throw std::bad_alloc();
        outputStream_->time_base = encoder_->time_base;
        CheckAudioResult(avcodec_parameters_from_context(outputStream_->codecpar, encoder_), "Failed to copy encoder parameters");
        CheckAudioResult(swr_alloc_set_opts2(&resampler_, &encoder_->ch_layout, encoder_->sample_fmt, encoder_->sample_rate,
                                           &decoder_->ch_layout, decoder_->sample_fmt, decoder_->sample_rate, 0, nullptr),
                         "Failed to allocate resampler");
        CheckAudioResult(swr_init(resampler_), "Failed to initialize resampler");

        decodedFrame_ = av_frame_alloc();
        resampledFrame_ = av_frame_alloc();
        encodedFrame_ = av_frame_alloc();
        inputPacket_ = av_packet_alloc();
        outputPacket_ = av_packet_alloc();
        fifo_ = av_audio_fifo_alloc(encoder_->sample_fmt, 1, 1);
        if (!decodedFrame_ || !resampledFrame_ || !encodedFrame_ || !inputPacket_ || !outputPacket_ || !fifo_)
            throw std::bad_alloc();

        if (!(output_->oformat->flags & AVFMT_NOFILE))
            CheckAudioResult(avio_open(&output_->pb, outputPath.c_str(), AVIO_FLAG_WRITE), "Failed to open output file");
        CheckAudioResult(avformat_write_header(output_, nullptr), "Failed to write audio header");

        int readResult;
        while ((readResult = av_read_frame(input_, inputPacket_)) >= 0)
        {
            if (inputPacket_->stream_index == audioStream)
            {
                CheckAudioResult(avcodec_send_packet(decoder_, inputPacket_), "Failed to send audio packet");
                DrainDecoder();
            }
            av_packet_unref(inputPacket_);
        }
        if (readResult != AVERROR_EOF)
            CheckAudioResult(readResult, "Failed to read audio packet");
        CheckAudioResult(avcodec_send_packet(decoder_, nullptr), "Failed to flush audio decoder");
        DrainDecoder();
        while (Resample(nullptr) > 0)
            DrainFifo(false);
        DrainFifo(true);
        CheckAudioResult(avcodec_send_frame(encoder_, nullptr), "Failed to flush audio encoder");
        DrainEncoder();
        CheckAudioResult(av_write_trailer(output_), "Failed to finish audio container");
        if (output_->pb)
            CheckAudioResult(avio_closep(&output_->pb), "Failed to close output file");
        return encoder_->sample_rate;
    }

private:
    static int SelectSampleRate(const AVCodec *codec, int requestedRate, int inputRate)
    {
        if (requestedRate < 0 || inputRate <= 0)
            throw std::runtime_error("Invalid audio sample rate");
        if (!codec->supported_samplerates)
            return requestedRate > 0 ? requestedRate : inputRate;
        int closest = codec->supported_samplerates[0];
        for (const int *rate = codec->supported_samplerates; *rate; ++rate)
        {
            if (*rate == requestedRate)
                return requestedRate;
            if (std::abs(*rate - inputRate) < std::abs(closest - inputRate))
                closest = *rate;
        }
        if (requestedRate > 0)
            throw std::runtime_error("Requested sample rate is not supported by the encoder");
        return closest;
    }

    void PrepareFrame(AVFrame *frame, int sampleCount)
    {
        av_frame_unref(frame);
        frame->format = encoder_->sample_fmt;
        frame->sample_rate = encoder_->sample_rate;
        frame->nb_samples = sampleCount;
        CheckAudioResult(av_channel_layout_copy(&frame->ch_layout, &encoder_->ch_layout), "Failed to copy channel layout");
        CheckAudioResult(av_frame_get_buffer(frame, 0), "Failed to allocate audio frame");
    }

    int Resample(const AVFrame *frame)
    {
        const int inputSamples = frame ? frame->nb_samples : 0;
        const int capacity = swr_get_out_samples(resampler_, inputSamples);
        CheckAudioResult(capacity, "Failed to size resampled audio");
        if (capacity == 0)
            return 0;
        PrepareFrame(resampledFrame_, capacity);
        const int converted = swr_convert(resampler_, resampledFrame_->extended_data, capacity,
                                          frame ? const_cast<const uint8_t **>(frame->extended_data) : nullptr, inputSamples);
        CheckAudioResult(converted, "Failed to resample audio");
        if (converted > 0)
            CheckAudioResult(av_audio_fifo_write(fifo_, reinterpret_cast<void **>(resampledFrame_->extended_data), converted),
                             "Failed to buffer resampled audio");
        return converted;
    }

    void DrainDecoder()
    {
        int decodeResult;
        while ((decodeResult = avcodec_receive_frame(decoder_, decodedFrame_)) >= 0)
        {
            Resample(decodedFrame_);
            av_frame_unref(decodedFrame_);
            DrainFifo(false);
        }
        if (decodeResult != AVERROR(EAGAIN) && decodeResult != AVERROR_EOF)
            CheckAudioResult(decodeResult, "Failed to decode audio");
    }

    void DrainFifo(bool final)
    {
        int available;
        while ((available = av_audio_fifo_size(fifo_)) > 0)
        {
            const int frameSize = encoder_->frame_size;
            if (!final && frameSize > available)
                break;
            const int samples = frameSize > 0 ? std::min(available, frameSize) : available;
            const bool pad = samples < frameSize && !(encoder_->codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME);
            PrepareFrame(encodedFrame_, pad ? frameSize : samples);
            CheckAudioResult(av_audio_fifo_read(fifo_, reinterpret_cast<void **>(encodedFrame_->extended_data), samples),
                             "Failed to read buffered audio");
            if (pad)
                CheckAudioResult(av_samples_set_silence(encodedFrame_->extended_data, samples, frameSize - samples, 1, encoder_->sample_fmt),
                                 "Failed to pad final audio frame");
            encodedFrame_->pts = nextPts_;
            nextPts_ += encodedFrame_->nb_samples;
            CheckAudioResult(avcodec_send_frame(encoder_, encodedFrame_), "Failed to encode audio frame");
            DrainEncoder();
        }
    }

    void DrainEncoder()
    {
        int encodeResult;
        while ((encodeResult = avcodec_receive_packet(encoder_, outputPacket_)) >= 0)
        {
            outputPacket_->stream_index = outputStream_->index;
            av_packet_rescale_ts(outputPacket_, encoder_->time_base, outputStream_->time_base);
            CheckAudioResult(av_interleaved_write_frame(output_, outputPacket_), "Failed to write encoded audio");
            av_packet_unref(outputPacket_);
        }
        if (encodeResult != AVERROR(EAGAIN) && encodeResult != AVERROR_EOF)
            CheckAudioResult(encodeResult, "Failed to receive encoded audio");
    }

    AVFormatContext *input_ = nullptr;
    AVFormatContext *output_ = nullptr;
    AVStream *outputStream_ = nullptr;
    AVCodecContext *decoder_ = nullptr;
    AVCodecContext *encoder_ = nullptr;
    SwrContext *resampler_ = nullptr;
    AVAudioFifo *fifo_ = nullptr;
    AVFrame *decodedFrame_ = nullptr;
    AVFrame *resampledFrame_ = nullptr;
    AVFrame *encodedFrame_ = nullptr;
    AVPacket *inputPacket_ = nullptr;
    AVPacket *outputPacket_ = nullptr;
    int64_t nextPts_ = 0;
};
}

int TranscodeAudio(const std::string &inputPath, const std::string &outputPath,
                   const std::string &format, int requestedSampleRate)
{
    const auto selectedFormat = audioFormats.find(format);
    if (selectedFormat == audioFormats.end())
        throw std::runtime_error("Unsupported output format");
    AudioPipeline pipeline;
    return pipeline.Convert(inputPath, outputPath, selectedFormat->second, requestedSampleRate);
}
