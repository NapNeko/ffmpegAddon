#include "decodeAudio.h"
#include <iostream>

// ===== DecodeAudioToPCM Async Worker =====
class DecodeAudioToPCMWorker : public AsyncWorker
{
public:
    DecodeAudioToPCMWorker(const std::string &path, Promise::Deferred deferred)
        : AsyncWorker(deferred.Env()), path_(path), deferred_(deferred), sampleRate_(0), channels_(0) {}

    void Execute() override
    {
        AVFormatContext *fmt = nullptr;
        if (avformat_open_input(&fmt, path_.c_str(), nullptr, nullptr) != 0)
        {
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

        AVChannelLayout tmp_ch_layout;
        const AVChannelLayout *in_ch_layout = &st->codecpar->ch_layout;
        if (in_ch_layout->nb_channels == 0)
        {
            av_channel_layout_default(&tmp_ch_layout, src_channels);
            in_ch_layout = &tmp_ch_layout;
        }

        SwrContext *swr = nullptr;
        if (swr_alloc_set_opts2(&swr,
                                in_ch_layout, AV_SAMPLE_FMT_S16, src_sample_rate,
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
                        if (av_samples_alloc_array_and_samples(&dst, nullptr, src_channels, dst_nb_samples, AV_SAMPLE_FMT_S16, 0) < 0)
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
                        int nb = av_samples_get_buffer_size(nullptr, src_channels, ret, AV_SAMPLE_FMT_S16, 1);
                        size_t prev = outbuf_.size();
                        outbuf_.resize(prev + nb);
                        memcpy(outbuf_.data() + prev, dst[0], nb);
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
        sampleRate_ = src_sample_rate;
        channels_ = src_channels;
        avcodec_free_context(&c);
        avformat_close_input(&fmt);
    }

    void OnOK() override
    {
        Napi::Env env = Env();
        Buffer<uint8_t> buf = Buffer<uint8_t>::Copy(env, outbuf_.data(), outbuf_.size());
        Object res = Object::New(env);
        res.Set("pcm", buf);
        res.Set("sampleRate", Number::New(env, sampleRate_));
        res.Set("channels", Number::New(env, channels_));
        deferred_.Resolve(res);
    }

    void OnError(const Error &e) override
    {
        deferred_.Reject(e.Value());
    }

private:
    std::string path_;
    Promise::Deferred deferred_;
    std::vector<uint8_t> outbuf_;
    int sampleRate_;
    int channels_;
};

Value DecodeAudioToPCM(const CallbackInfo &info)
{
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString())
    {
        TypeError::New(env, "Expected a file path string").ThrowAsJavaScriptException();
        return env.Null();
    }
    std::string path = info[0].As<String>().Utf8Value();
    Promise::Deferred deferred = Promise::Deferred::New(env);
    DecodeAudioToPCMWorker *worker = new DecodeAudioToPCMWorker(path, deferred);
    worker->Queue();
    return deferred.Promise();
}

