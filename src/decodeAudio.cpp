#include "decodeAudio.h"
#include <iostream>
#include <map>

// 格式配置结构
struct FormatConfig
{
    const char *format_name;     // 容器格式
    enum AVCodecID codec_id;     // 编码器ID
    enum AVSampleFormat sample_fmt; // 采样格式
    int bit_rate;                // 比特率
};

// 支持的输出格式映射
static const std::map<std::string, FormatConfig> FORMAT_CONFIGS = {
    {"mp3", {"mp3", AV_CODEC_ID_MP3, AV_SAMPLE_FMT_S16P, 128000}},
    {"amr", {"amr", AV_CODEC_ID_AMR_NB, AV_SAMPLE_FMT_S16, 12200}},
    {"wma", {"asf", AV_CODEC_ID_WMAV2, AV_SAMPLE_FMT_FLTP, 128000}},
    {"m4a", {"ipod", AV_CODEC_ID_AAC, AV_SAMPLE_FMT_FLTP, 128000}},
    {"spx", {"ogg", AV_CODEC_ID_SPEEX, AV_SAMPLE_FMT_S16, 24600}},
    {"ogg", {"ogg", AV_CODEC_ID_VORBIS, AV_SAMPLE_FMT_FLTP, 128000}},
    {"wav", {"wav", AV_CODEC_ID_PCM_S16LE, AV_SAMPLE_FMT_S16, 0}},
    {"flac", {"flac", AV_CODEC_ID_FLAC, AV_SAMPLE_FMT_S16, 0}}
};

// ===== DecodeAudioToFmt Async Worker =====
class DecodeAudioToFmtWorker : public AsyncWorker
{
public:
    DecodeAudioToFmtWorker(const std::string &inputPath, const std::string &outputPath, 
                           const std::string &targetFormat, int targetSampleRate, Promise::Deferred deferred)
        : AsyncWorker(deferred.Env()), inputPath_(inputPath), outputPath_(outputPath), 
          targetFormat_(targetFormat), targetSampleRate_(targetSampleRate), 
          deferred_(deferred), sampleRate_(0), channels_(0) {}

    void Execute() override
    {
        // 检查目标格式是否支持
        auto it = FORMAT_CONFIGS.find(targetFormat_);
        if (it == FORMAT_CONFIGS.end())
        {
            SetError("Unsupported output format. Supported formats: mp3, amr, wma, m4a, spx, ogg, wav, flac");
            return;
        }
        const FormatConfig &config = it->second;

        // 打开输入文件
        AVFormatContext *input_fmt_ctx = nullptr;
        if (avformat_open_input(&input_fmt_ctx, inputPath_.c_str(), nullptr, nullptr) != 0)
        {
            SetError("Failed to open input file");
            return;
        }
        if (avformat_find_stream_info(input_fmt_ctx, nullptr) < 0)
        {
            avformat_close_input(&input_fmt_ctx);
            SetError("Failed to find stream info");
            return;
        }

        // 查找音频流
        int audio_stream_index = -1;
        for (unsigned i = 0; i < input_fmt_ctx->nb_streams; ++i)
        {
            if (input_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                audio_stream_index = i;
                break;
            }
        }
        if (audio_stream_index < 0)
        {
            avformat_close_input(&input_fmt_ctx);
            SetError("No audio stream found");
            return;
        }

        AVStream *input_stream = input_fmt_ctx->streams[audio_stream_index];
        
        // 初始化解码器
        const AVCodec *decoder = avcodec_find_decoder(input_stream->codecpar->codec_id);
        if (!decoder)
        {
            avformat_close_input(&input_fmt_ctx);
            SetError("Decoder not found");
            return;
        }
        
        AVCodecContext *decoder_ctx = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(decoder_ctx, input_stream->codecpar);
        if (avcodec_open2(decoder_ctx, decoder, nullptr) < 0)
        {
            avcodec_free_context(&decoder_ctx);
            avformat_close_input(&input_fmt_ctx);
            SetError("Failed to open decoder");
            return;
        }

        // 获取输入音频参数
        int src_channels = input_stream->codecpar->ch_layout.nb_channels ? input_stream->codecpar->ch_layout.nb_channels : 1;
        int src_sample_rate = input_stream->codecpar->sample_rate;
        enum AVSampleFormat src_sample_fmt = (enum AVSampleFormat)input_stream->codecpar->format;

        // 确定输出采样率
        int out_sample_rate;
        if (targetSampleRate_ > 0)
        {
            out_sample_rate = targetSampleRate_;
        }
        else
        {
            // 支持的采样率列表
            const int supported_rates[] = {48000, 44100, 32000, 24000, 16000, 12000, 8000};
            const int num_rates = sizeof(supported_rates) / sizeof(supported_rates[0]);
            
            // 找到最接近的采样率
            int closest_rate = supported_rates[0];
            int min_diff = abs(src_sample_rate - supported_rates[0]);
            
            for (int i = 1; i < num_rates; ++i)
            {
                int diff = abs(src_sample_rate - supported_rates[i]);
                if (diff < min_diff)
                {
                    min_diff = diff;
                    closest_rate = supported_rates[i];
                }
            }
            
            out_sample_rate = closest_rate;
        }

        // AMR只支持8000Hz采样率
        if (config.codec_id == AV_CODEC_ID_AMR_NB)
        {
            out_sample_rate = 8000;
        }

        // 设置输出声道布局(单声道)
        AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
        int out_channels = 1;

        // 初始化编码器
        const AVCodec *encoder = avcodec_find_encoder(config.codec_id);
        if (!encoder)
        {
            avcodec_free_context(&decoder_ctx);
            avformat_close_input(&input_fmt_ctx);
            SetError("Encoder not found");
            return;
        }

        AVCodecContext *encoder_ctx = avcodec_alloc_context3(encoder);
        encoder_ctx->sample_rate = out_sample_rate;
        encoder_ctx->ch_layout = out_ch_layout;
        
        // 检查编码器支持的采样格式
        if (encoder->sample_fmts)
        {
            const enum AVSampleFormat *p = encoder->sample_fmts;
            bool format_supported = false;
            while (*p != AV_SAMPLE_FMT_NONE)
            {
                if (*p == config.sample_fmt)
                {
                    format_supported = true;
                    break;
                }
                p++;
            }
            if (!format_supported && encoder->sample_fmts[0] != AV_SAMPLE_FMT_NONE)
            {
                encoder_ctx->sample_fmt = encoder->sample_fmts[0];
            }
            else
            {
                encoder_ctx->sample_fmt = config.sample_fmt;
            }
        }
        else
        {
            encoder_ctx->sample_fmt = config.sample_fmt;
        }
        
        if (config.bit_rate > 0)
        {
            encoder_ctx->bit_rate = config.bit_rate;
        }
        
        // FLAC特殊设置
        if (config.codec_id == AV_CODEC_ID_FLAC)
        {
            encoder_ctx->compression_level = 5;
        }

        if (avcodec_open2(encoder_ctx, encoder, nullptr) < 0)
        {
            avcodec_free_context(&encoder_ctx);
            avcodec_free_context(&decoder_ctx);
            avformat_close_input(&input_fmt_ctx);
            SetError("Failed to open encoder");
            return;
        }

        // 创建输出格式上下文
        AVFormatContext *output_fmt_ctx = nullptr;
        if (avformat_alloc_output_context2(&output_fmt_ctx, nullptr, config.format_name, outputPath_.c_str()) < 0)
        {
            avcodec_free_context(&encoder_ctx);
            avcodec_free_context(&decoder_ctx);
            avformat_close_input(&input_fmt_ctx);
            SetError("Failed to create output context");
            return;
        }

        // 添加音频流
        AVStream *output_stream = avformat_new_stream(output_fmt_ctx, nullptr);
        if (!output_stream)
        {
            avformat_free_context(output_fmt_ctx);
            avcodec_free_context(&encoder_ctx);
            avcodec_free_context(&decoder_ctx);
            avformat_close_input(&input_fmt_ctx);
            SetError("Failed to create output stream");
            return;
        }

        avcodec_parameters_from_context(output_stream->codecpar, encoder_ctx);
        output_stream->time_base.num = 1;
        output_stream->time_base.den = out_sample_rate;

        // 打开输出文件
        if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE))
        {
            if (avio_open(&output_fmt_ctx->pb, outputPath_.c_str(), AVIO_FLAG_WRITE) < 0)
            {
                avformat_free_context(output_fmt_ctx);
                avcodec_free_context(&encoder_ctx);
                avcodec_free_context(&decoder_ctx);
                avformat_close_input(&input_fmt_ctx);
                SetError("Failed to open output file");
                return;
            }
        }

        // 写入文件头
        if (avformat_write_header(output_fmt_ctx, nullptr) < 0)
        {
            if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&output_fmt_ctx->pb);
            avformat_free_context(output_fmt_ctx);
            avcodec_free_context(&encoder_ctx);
            avcodec_free_context(&decoder_ctx);
            avformat_close_input(&input_fmt_ctx);
            SetError("Failed to write header");
            return;
        }

        // 设置输入声道布局
        AVChannelLayout tmp_ch_layout;
        bool tmp_ch_layout_allocated = false;
        const AVChannelLayout *in_ch_layout = &input_stream->codecpar->ch_layout;
        if (in_ch_layout->nb_channels == 0)
        {
            av_channel_layout_default(&tmp_ch_layout, src_channels);
            in_ch_layout = &tmp_ch_layout;
            tmp_ch_layout_allocated = true;
        }

        // 初始化重采样器 - 使用编码器实际的采样格式
        SwrContext *swr_ctx = nullptr;
        if (swr_alloc_set_opts2(&swr_ctx,
                                &out_ch_layout, encoder_ctx->sample_fmt, out_sample_rate,
                                in_ch_layout, src_sample_fmt, src_sample_rate,
                                0, nullptr) < 0 || !swr_ctx)
        {
            if (swr_ctx)
                swr_free(&swr_ctx);
            if (tmp_ch_layout_allocated)
                av_channel_layout_uninit(&tmp_ch_layout);
            if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&output_fmt_ctx->pb);
            avformat_free_context(output_fmt_ctx);
            avcodec_free_context(&encoder_ctx);
            avcodec_free_context(&decoder_ctx);
            avformat_close_input(&input_fmt_ctx);
            SetError("Failed to initialize resampler");
            return;
        }
        if (swr_init(swr_ctx) < 0)
        {
            swr_free(&swr_ctx);
            if (tmp_ch_layout_allocated)
                av_channel_layout_uninit(&tmp_ch_layout);
            if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&output_fmt_ctx->pb);
            avformat_free_context(output_fmt_ctx);
            avcodec_free_context(&encoder_ctx);
            avcodec_free_context(&decoder_ctx);
            avformat_close_input(&input_fmt_ctx);
            SetError("Failed to initialize resampler");
            return;
        }

        // 解码和编码
        AVPacket *input_packet = av_packet_alloc();
        AVFrame *decoded_frame = av_frame_alloc();
        AVFrame *resampled_frame = av_frame_alloc();
        AVPacket *output_packet = av_packet_alloc();

        // 获取编码器期望的帧大小
        int frame_size = encoder_ctx->frame_size;
        if (frame_size == 0)
        {
            frame_size = 1152; // 默认帧大小
        }

        // 创建FIFO缓冲区用于累积样本
        AVAudioFifo *fifo = av_audio_fifo_alloc(encoder_ctx->sample_fmt, out_channels, frame_size * 2);
        if (!fifo)
        {
            av_packet_free(&output_packet);
            av_frame_free(&resampled_frame);
            av_frame_free(&decoded_frame);
            av_packet_free(&input_packet);
            swr_free(&swr_ctx);
            if (tmp_ch_layout_allocated)
                av_channel_layout_uninit(&tmp_ch_layout);
            if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&output_fmt_ctx->pb);
            avformat_free_context(output_fmt_ctx);
            avcodec_free_context(&encoder_ctx);
            avcodec_free_context(&decoder_ctx);
            avformat_close_input(&input_fmt_ctx);
            SetError("Failed to allocate FIFO");
            return;
        }

        int64_t pts = 0;

        while (av_read_frame(input_fmt_ctx, input_packet) >= 0)
        {
            if (input_packet->stream_index == audio_stream_index)
            {
                if (avcodec_send_packet(decoder_ctx, input_packet) == 0)
                {
                    while (avcodec_receive_frame(decoder_ctx, decoded_frame) == 0)
                    {
                        // 计算重采样后的样本数
                        int dst_nb_samples = av_rescale_rnd(
                            swr_get_delay(swr_ctx, src_sample_rate) + decoded_frame->nb_samples,
                            out_sample_rate, src_sample_rate, AV_ROUND_UP);

                        // 分配重采样帧
                        resampled_frame->format = encoder_ctx->sample_fmt;
                        resampled_frame->ch_layout = out_ch_layout;
                        resampled_frame->sample_rate = out_sample_rate;
                        resampled_frame->nb_samples = dst_nb_samples;

                        if (av_frame_get_buffer(resampled_frame, 0) < 0)
                        {
                            continue;
                        }

                        // 执行重采样
                        int converted_samples = swr_convert(swr_ctx,
                                                           resampled_frame->data, dst_nb_samples,
                                                           (const uint8_t **)decoded_frame->data, decoded_frame->nb_samples);

                        if (converted_samples > 0)
                        {
                            // 将重采样的数据写入FIFO
                            av_audio_fifo_write(fifo, (void **)resampled_frame->data, converted_samples);

                            // 当FIFO中有足够的样本时,编码帧
                            while (av_audio_fifo_size(fifo) >= frame_size)
                            {
                                AVFrame *encode_frame = av_frame_alloc();
                                encode_frame->format = encoder_ctx->sample_fmt;
                                encode_frame->ch_layout = out_ch_layout;
                                encode_frame->sample_rate = out_sample_rate;
                                encode_frame->nb_samples = frame_size;

                                if (av_frame_get_buffer(encode_frame, 0) >= 0)
                                {
                                    av_audio_fifo_read(fifo, (void **)encode_frame->data, frame_size);
                                    encode_frame->pts = pts;
                                    pts += frame_size;

                                    if (avcodec_send_frame(encoder_ctx, encode_frame) == 0)
                                    {
                                        while (avcodec_receive_packet(encoder_ctx, output_packet) == 0)
                                        {
                                            output_packet->stream_index = 0;
                                            av_packet_rescale_ts(output_packet, encoder_ctx->time_base, output_stream->time_base);
                                            av_interleaved_write_frame(output_fmt_ctx, output_packet);
                                            av_packet_unref(output_packet);
                                        }
                                    }
                                }
                                av_frame_free(&encode_frame);
                            }
                        }

                        av_frame_unref(resampled_frame);
                    }
                }
            }
            av_packet_unref(input_packet);
        }

        // 刷新解码器
        avcodec_send_packet(decoder_ctx, nullptr);
        while (avcodec_receive_frame(decoder_ctx, decoded_frame) == 0)
        {
            int dst_nb_samples = av_rescale_rnd(
                swr_get_delay(swr_ctx, src_sample_rate) + decoded_frame->nb_samples,
                out_sample_rate, src_sample_rate, AV_ROUND_UP);

            resampled_frame->format = encoder_ctx->sample_fmt;
            resampled_frame->ch_layout = out_ch_layout;
            resampled_frame->sample_rate = out_sample_rate;
            resampled_frame->nb_samples = dst_nb_samples;

            if (av_frame_get_buffer(resampled_frame, 0) >= 0)
            {
                int converted_samples = swr_convert(swr_ctx,
                                                   resampled_frame->data, dst_nb_samples,
                                                   (const uint8_t **)decoded_frame->data, decoded_frame->nb_samples);

                if (converted_samples > 0)
                {
                    av_audio_fifo_write(fifo, (void **)resampled_frame->data, converted_samples);

                    while (av_audio_fifo_size(fifo) >= frame_size)
                    {
                        AVFrame *encode_frame = av_frame_alloc();
                        encode_frame->format = encoder_ctx->sample_fmt;
                        encode_frame->ch_layout = out_ch_layout;
                        encode_frame->sample_rate = out_sample_rate;
                        encode_frame->nb_samples = frame_size;

                        if (av_frame_get_buffer(encode_frame, 0) >= 0)
                        {
                            av_audio_fifo_read(fifo, (void **)encode_frame->data, frame_size);
                            encode_frame->pts = pts;
                            pts += frame_size;

                            if (avcodec_send_frame(encoder_ctx, encode_frame) == 0)
                            {
                                while (avcodec_receive_packet(encoder_ctx, output_packet) == 0)
                                {
                                    output_packet->stream_index = 0;
                                    av_packet_rescale_ts(output_packet, encoder_ctx->time_base, output_stream->time_base);
                                    av_interleaved_write_frame(output_fmt_ctx, output_packet);
                                    av_packet_unref(output_packet);
                                }
                            }
                        }
                        av_frame_free(&encode_frame);
                    }
                }
            }
            av_frame_unref(resampled_frame);
        }

        // 刷新重采样器中剩余的样本
        int converted_samples = swr_convert(swr_ctx, resampled_frame->data, frame_size, nullptr, 0);
        if (converted_samples > 0)
        {
            av_audio_fifo_write(fifo, (void **)resampled_frame->data, converted_samples);
        }

        // 处理FIFO中剩余的样本
        while (av_audio_fifo_size(fifo) > 0)
        {
            int remaining = av_audio_fifo_size(fifo);
            AVFrame *encode_frame = av_frame_alloc();
            encode_frame->format = encoder_ctx->sample_fmt;
            encode_frame->ch_layout = out_ch_layout;
            encode_frame->sample_rate = out_sample_rate;
            encode_frame->nb_samples = remaining;

            if (av_frame_get_buffer(encode_frame, 0) >= 0)
            {
                av_audio_fifo_read(fifo, (void **)encode_frame->data, remaining);
                encode_frame->pts = pts;
                pts += remaining;

                if (avcodec_send_frame(encoder_ctx, encode_frame) == 0)
                {
                    while (avcodec_receive_packet(encoder_ctx, output_packet) == 0)
                    {
                        output_packet->stream_index = 0;
                        av_packet_rescale_ts(output_packet, encoder_ctx->time_base, output_stream->time_base);
                        av_interleaved_write_frame(output_fmt_ctx, output_packet);
                        av_packet_unref(output_packet);
                    }
                }
            }
            av_frame_free(&encode_frame);
            break;
        }

        // 刷新编码器
        avcodec_send_frame(encoder_ctx, nullptr);
        while (avcodec_receive_packet(encoder_ctx, output_packet) == 0)
        {
            output_packet->stream_index = 0;
            av_packet_rescale_ts(output_packet, encoder_ctx->time_base, output_stream->time_base);
            av_interleaved_write_frame(output_fmt_ctx, output_packet);
            av_packet_unref(output_packet);
        }

        // 清理FIFO
        av_audio_fifo_free(fifo);

        // 写入文件尾
        av_write_trailer(output_fmt_ctx);

        // 清理资源
        av_packet_free(&output_packet);
        av_frame_free(&resampled_frame);
        av_frame_free(&decoded_frame);
        av_packet_free(&input_packet);
        swr_free(&swr_ctx);
        if (tmp_ch_layout_allocated)
            av_channel_layout_uninit(&tmp_ch_layout);
        
        if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&output_fmt_ctx->pb);
        avformat_free_context(output_fmt_ctx);
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_close_input(&input_fmt_ctx);

        sampleRate_ = out_sample_rate;
        channels_ = out_channels;
    }

    void OnOK() override
    {
        Napi::Env env = Env();
        Object res = Object::New(env);
        res.Set("result", Boolean::New(env, true));
        res.Set("sampleRate", Number::New(env, sampleRate_));
        res.Set("channels", Number::New(env, channels_));
        res.Set("format", String::New(env, targetFormat_));
        deferred_.Resolve(res);
    }

    void OnError(const Error &e) override
    {
        deferred_.Reject(e.Value());
    }

private:
    std::string inputPath_;
    std::string outputPath_;
    std::string targetFormat_;
    int targetSampleRate_;
    Promise::Deferred deferred_;
    int sampleRate_;
    int channels_;
};

Value DecodeAudioToFmt(const CallbackInfo &info)
{
    Env env = info.Env();
    if (info.Length() < 3 || !info[0].IsString() || !info[1].IsString() || !info[2].IsString())
    {
        TypeError::New(env, "Expected inputPath (string), outputPath (string), and targetFormat (string)").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    std::string inputPath = info[0].As<String>().Utf8Value();
    std::string outputPath = info[1].As<String>().Utf8Value();
    std::string targetFormat = info[2].As<String>().Utf8Value();
    
    // 第四个参数可选:目标采样率
    int targetSampleRate = 0; // 0 表示自动选择最接近的采样率
    if (info.Length() >= 4 && info[3].IsNumber())
    {
        targetSampleRate = info[3].As<Number>().Int32Value();
    }
    
    Promise::Deferred deferred = Promise::Deferred::New(env);
    DecodeAudioToFmtWorker *worker = new DecodeAudioToFmtWorker(inputPath, outputPath, targetFormat, targetSampleRate, deferred);
    worker->Queue();
    return deferred.Promise();
}
