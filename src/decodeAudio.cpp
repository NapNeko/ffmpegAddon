#include "decodeAudio.h"
#include <iostream>

// ===== DecodeAudioToPCM Async Worker =====
class DecodeAudioToPCMWorker : public AsyncWorker
{
public:
    DecodeAudioToPCMWorker(const std::string &inputPath, const std::string &outputPath, int targetSampleRate, Promise::Deferred deferred)
        : AsyncWorker(deferred.Env()), inputPath_(inputPath), outputPath_(outputPath), targetSampleRate_(targetSampleRate), deferred_(deferred), sampleRate_(0), channels_(0) {}

    void Execute() override
    {
        // 打开输出文件
        FILE *outFile = nullptr;
        if (!outputPath_.empty())
        {
            outFile = fopen(outputPath_.c_str(), "wb");
            if (!outFile)
            {
                SetError("Failed to open output file");
                return;
            }
        }

        AVFormatContext *fmt = nullptr;
        if (avformat_open_input(&fmt, inputPath_.c_str(), nullptr, nullptr) != 0)
        {
            if (outFile)
                fclose(outFile);
            SetError("Failed to open input");
            return;
        }
        if (avformat_find_stream_info(fmt, nullptr) < 0)
        {
            avformat_close_input(&fmt);
            SetError("Failed to find stream info");
            return;
        }
        int audStream = -1;
        for (unsigned i = 0; i < fmt->nb_streams; ++i)
        {
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                audStream = i;
                break;
            }
        }
        if (audStream < 0)
        {
            avformat_close_input(&fmt);
            SetError("No audio stream");
            return;
        }
        AVStream *st = fmt->streams[audStream];
        const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec)
        {
            avformat_close_input(&fmt);
            SetError("Decoder not found");
            return;
        }
        AVCodecContext *c = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(c, st->codecpar);
        if (avcodec_open2(c, dec, nullptr) < 0)
        {
            avcodec_free_context(&c);
            avformat_close_input(&fmt);
            SetError("Failed to open codec");
            return;
        }

        int src_channels = st->codecpar->ch_layout.nb_channels ? st->codecpar->ch_layout.nb_channels : 1;
        int src_sample_rate = st->codecpar->sample_rate;
        enum AVSampleFormat src_sample_fmt = (enum AVSampleFormat)st->codecpar->format;

        // 如果指定了目标采样率,使用它;否则自动选择最接近的采样率
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

        AVChannelLayout tmp_ch_layout;
        const AVChannelLayout *in_ch_layout = &st->codecpar->ch_layout;
        if (in_ch_layout->nb_channels == 0)
        {
            av_channel_layout_default(&tmp_ch_layout, src_channels);
            in_ch_layout = &tmp_ch_layout;
        }

        // 输出单声道
        AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_MONO;

        SwrContext *swr = nullptr;
        if (swr_alloc_set_opts2(&swr,
                                &out_ch_layout, AV_SAMPLE_FMT_S16, out_sample_rate,
                                in_ch_layout, src_sample_fmt, src_sample_rate,
                                0, nullptr) < 0 ||
            !swr)
        {
            if (swr)
                swr_free(&swr);
            avcodec_free_context(&c);
            avformat_close_input(&fmt);
            SetError("Failed to init resampler");
            return;
        }
        if (swr_init(swr) < 0)
        {
            swr_free(&swr);
            avcodec_free_context(&c);
            avformat_close_input(&fmt);
            SetError("Failed to init resampler");
            return;
        }

        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        while (av_read_frame(fmt, pkt) >= 0)
        {
            if (pkt->stream_index == audStream)
            {
                if (avcodec_send_packet(c, pkt) == 0)
                {
                    while (avcodec_receive_frame(c, frame) == 0)
                    {
                        uint8_t **dst = nullptr;
                        int dst_nb_samples = swr_get_out_samples(swr, frame->nb_samples);
                        if (dst_nb_samples <= 0)
                            dst_nb_samples = frame->nb_samples;
                        if (av_samples_alloc_array_and_samples(&dst, nullptr, 1, dst_nb_samples, AV_SAMPLE_FMT_S16, 0) < 0)
                        {
                            continue;
                        }
                        int ret = swr_convert(swr, (uint8_t *const *)dst, dst_nb_samples, (const uint8_t **)frame->data, frame->nb_samples);
                        if (ret < 0)
                        {
                            av_freep(&dst[0]);
                            av_freep(&dst);
                            continue;
                        }
                        int nb = av_samples_get_buffer_size(nullptr, 1, ret, AV_SAMPLE_FMT_S16, 1);
                        if (outFile)
                        {
                            fwrite(dst[0], 1, nb, outFile);
                        }
                        av_freep(&dst[0]);
                        av_freep(&dst);
                    }
                }
            }
            av_packet_unref(pkt);
        }
        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        sampleRate_ = out_sample_rate;
        channels_ = 1; // 输出单声道
        avcodec_free_context(&c);
        avformat_close_input(&fmt);

        // 关闭输出文件
        if (outFile)
        {
            fclose(outFile);
        }
    }

    void OnOK() override
    {
        Napi::Env env = Env();
        Object res = Object::New(env);
        res.Set("result", Boolean::New(env, true));
        res.Set("sampleRate", Number::New(env, sampleRate_));
        deferred_.Resolve(res);
    }

    void OnError(const Error &e) override
    {
        deferred_.Reject(e.Value());
    }

private:
    std::string inputPath_;
    std::string outputPath_;
    int targetSampleRate_;
    Promise::Deferred deferred_;
    int sampleRate_;
    int channels_;
};

Value DecodeAudioToPCM(const CallbackInfo &info)
{
    Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString())
    {
        TypeError::New(env, "Expected inputPath (string) and outputPath (string)").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    std::string inputPath = info[0].As<String>().Utf8Value();
    std::string outputPath = info[1].As<String>().Utf8Value();
    
    // 第三个参数可选:目标采样率
    int targetSampleRate = 0; // 0 表示不改变采样率
    if (info.Length() >= 3 && info[2].IsNumber())
    {
        targetSampleRate = info[2].As<Number>().Int32Value();
    }
    
    Promise::Deferred deferred = Promise::Deferred::New(env);
    DecodeAudioToPCMWorker *worker = new DecodeAudioToPCMWorker(inputPath, outputPath, targetSampleRate, deferred);
    worker->Queue();
    return deferred.Promise();
}

