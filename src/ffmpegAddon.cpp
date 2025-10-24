// FFmpeg N-API addon (trimmed build expectations)
#include <napi.h>
#include <iostream>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

using namespace Napi;

// Forward declarations
Value GetDuration(const CallbackInfo &info);
Value DecodeAudioToPCM(const CallbackInfo &info);
Value GetVideoInfo(const CallbackInfo &info);
Value ConvertToNTSilkTct(const CallbackInfo &info);

// Supported targets (intended to be enabled in FFmpeg build):
// - Containers (for cover & duration): avi, matroska (mkv), mov, mp4
// - Audio formats: mp3, amr, m4a(aac), ogg(vorbis), wav (pcm), flac
// The build script should enable only needed demuxers/decoders to keep size small.

// ===== GetDuration Async Worker =====
class GetDurationWorker : public AsyncWorker
{
public:
    GetDurationWorker(const std::string &path, Promise::Deferred deferred)
        : AsyncWorker(deferred.Env()), path_(path), deferred_(deferred), duration_(0.0) {}

    void Execute() override
    {
        AVFormatContext *fmt = nullptr;
        int ret = avformat_open_input(&fmt, path_.c_str(), nullptr, nullptr);
        if (ret < 0)
        {
            char buf[256];
            av_strerror(ret, buf, sizeof(buf));
            SetError(std::string("Failed to open input: ") + buf);
            return;
        }
        if ((ret = avformat_find_stream_info(fmt, nullptr)) < 0)
        {
            avformat_close_input(&fmt);
            char buf[256];
            av_strerror(ret, buf, sizeof(buf));
            SetError(std::string("Failed to find stream info: ") + buf);
            return;
        }
        if (fmt->duration != AV_NOPTS_VALUE)
        {
            duration_ = fmt->duration / (double)AV_TIME_BASE;
        }
        else
        {
            for (unsigned i = 0; i < fmt->nb_streams; ++i)
            {
                AVStream *st = fmt->streams[i];
                if (st->duration != AV_NOPTS_VALUE)
                {
                    double d = (double)st->duration * av_q2d(st->time_base);
                    if (d > duration_)
                        duration_ = d;
                }
            }
        }
        avformat_close_input(&fmt);
    }

    void OnOK() override
    {
        deferred_.Resolve(Number::New(Env(), duration_));
    }

    void OnError(const Error &e) override
    {
        deferred_.Reject(e.Value());
    }

private:
    std::string path_;
    Promise::Deferred deferred_;
    double duration_;
};

Value GetDuration(const CallbackInfo &info)
{
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString())
    {
        TypeError::New(env, "Expected a file path string").ThrowAsJavaScriptException();
        return env.Null();
    }
    std::string path = info[0].As<String>().Utf8Value();
    Promise::Deferred deferred = Promise::Deferred::New(env);
    GetDurationWorker *worker = new GetDurationWorker(path, deferred);
    worker->Queue();
    return deferred.Promise();
}
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

// ===== GetVideoInfo Async Worker =====
class GetVideoInfoWorker : public AsyncWorker
{
public:
    GetVideoInfoWorker(const std::string &path, const std::string &fmtStr, Promise::Deferred deferred)
        : AsyncWorker(deferred.Env()), path_(path), fmtStr_(fmtStr), deferred_(deferred),
          width_(0), height_(0), duration_(0.0) {}

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
        int vidStream = -1;
        for (unsigned i = 0; i < fmt->nb_streams; ++i)
        {
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                vidStream = i;
                break;
            }
        }
        if (vidStream < 0)
        {
            avformat_close_input(&fmt);
            SetError("No video stream");
            return;
        }
        AVStream *st = fmt->streams[vidStream];
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

        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        AVFrame *rgb = av_frame_alloc();
        struct SwsContext *sws = nullptr;
        int ret;
        bool success = false;
        while ((ret = av_read_frame(fmt, pkt)) >= 0)
        {
            if (pkt->stream_index == vidStream)
            {
                ret = avcodec_send_packet(c, pkt);
                if (ret < 0)
                {
                    av_packet_unref(pkt);
                    continue;
                }
                ret = avcodec_receive_frame(c, frame);
                if (ret == 0)
                {
                    int w = frame->width, h = frame->height;
                    width_ = w;
                    height_ = h;
                    int rgbLinesize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, w, h, 1);
                    uint8_t *buffer = (uint8_t *)av_malloc(rgbLinesize);
                    if (!buffer)
                    {
                        av_packet_unref(pkt);
                        break;
                    }
                    int afill = av_image_fill_arrays(rgb->data, rgb->linesize, buffer, AV_PIX_FMT_RGB24, w, h, 1);
                    if (afill < 0)
                    {
                        av_free(buffer);
                        av_packet_unref(pkt);
                        break;
                    }
                    sws = sws_getContext(w, h, (AVPixelFormat)frame->format, w, h, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (!sws)
                    {
                        av_free(buffer);
                        av_packet_unref(pkt);
                        break;
                    }
                    int scaled = sws_scale(sws, frame->data, frame->linesize, 0, h, rgb->data, rgb->linesize);
                    if (scaled <= 0)
                    {
                        av_free(buffer);
                        sws_freeContext(sws);
                        av_packet_unref(pkt);
                        break;
                    }

                    if (fmtStr_ == "bmp" || fmtStr_ == "bmp24")
                    {
                        int pad = (4 - (w * 3) % 4) % 4;
                        uint32_t rowSize = w * 3 + pad;
                        uint32_t dataSize = rowSize * h;
                        uint32_t fileSize = 14 + 40 + dataSize;
                        bmpData_.resize(14 + 40 + dataSize);
                        uint8_t *p = bmpData_.data();
                        // BITMAPFILEHEADER
                        p[0] = 'B';
                        p[1] = 'M';
                        p[2] = (uint8_t)(fileSize & 0xFF);
                        p[3] = (uint8_t)((fileSize >> 8) & 0xFF);
                        p[4] = (uint8_t)((fileSize >> 16) & 0xFF);
                        p[5] = (uint8_t)((fileSize >> 24) & 0xFF);
                        p[6] = p[7] = p[8] = p[9] = 0;
                        uint32_t offset = 14 + 40;
                        p[10] = (uint8_t)(offset & 0xFF);
                        p[11] = (uint8_t)((offset >> 8) & 0xFF);
                        p[12] = (uint8_t)((offset >> 16) & 0xFF);
                        p[13] = (uint8_t)((offset >> 24) & 0xFF);
                        // BITMAPINFOHEADER
                        uint8_t *q = p + 14;
                        uint32_t biSize = 40;
                        q[0] = (uint8_t)(biSize & 0xFF);
                        q[1] = (uint8_t)((biSize >> 8) & 0xFF);
                        q[2] = (uint8_t)((biSize >> 16) & 0xFF);
                        q[3] = (uint8_t)((biSize >> 24) & 0xFF);
                        // width
                        q[4] = (uint8_t)(w & 0xFF);
                        q[5] = (uint8_t)((w >> 8) & 0xFF);
                        q[6] = (uint8_t)((w >> 16) & 0xFF);
                        q[7] = (uint8_t)((w >> 24) & 0xFF);
                        // height (positive for bottom-up)
                        q[8] = (uint8_t)(h & 0xFF);
                        q[9] = (uint8_t)((h >> 8) & 0xFF);
                        q[10] = (uint8_t)((h >> 16) & 0xFF);
                        q[11] = (uint8_t)((h >> 24) & 0xFF);
                        // planes
                        q[12] = 1;
                        q[13] = 0;
                        // bit count
                        q[14] = 24;
                        q[15] = 0;
                        // compression
                        q[16] = q[17] = q[18] = q[19] = 0;
                        // image size
                        q[20] = (uint8_t)(dataSize & 0xFF);
                        q[21] = (uint8_t)((dataSize >> 8) & 0xFF);
                        q[22] = (uint8_t)((dataSize >> 16) & 0xFF);
                        q[23] = (uint8_t)((dataSize >> 24) & 0xFF);
                        // xppm, yppm
                        uint32_t ppm = 2835; // 72 DPI
                        q[24] = (uint8_t)(ppm & 0xFF);
                        q[25] = (uint8_t)((ppm >> 8) & 0xFF);
                        q[26] = (uint8_t)((ppm >> 16) & 0xFF);
                        q[27] = (uint8_t)((ppm >> 24) & 0xFF);
                        q[28] = (uint8_t)(ppm & 0xFF);
                        q[29] = (uint8_t)((ppm >> 8) & 0xFF);
                        q[30] = (uint8_t)((ppm >> 16) & 0xFF);
                        q[31] = (uint8_t)((ppm >> 24) & 0xFF);
                        // clrUsed, clrImportant
                        q[32] = q[33] = q[34] = q[35] = 0;
                        q[36] = q[37] = q[38] = q[39] = 0;
                        // pixel data: BMP is BGR bottom-up
                        uint8_t *dst = p + offset;
                        uint8_t *src = buffer;
                        for (int y = h - 1; y >= 0; --y)
                        {
                            uint8_t *rowDst = dst + (uint32_t)(h - 1 - y) * rowSize;
                            uint8_t *rowSrc = src + (uint32_t)y * w * 3;
                            for (int x = 0; x < w; ++x)
                            {
                                uint8_t r = rowSrc[x * 3 + 0];
                                uint8_t g = rowSrc[x * 3 + 1];
                                uint8_t b = rowSrc[x * 3 + 2];
                                rowDst[x * 3 + 0] = b;
                                rowDst[x * 3 + 1] = g;
                                rowDst[x * 3 + 2] = r;
                            }
                            for (uint32_t pz = 0; pz < (uint32_t)pad; ++pz)
                                rowDst[w * 3 + pz] = 0;
                        }
                        
                        if (fmt->duration != AV_NOPTS_VALUE)
                            duration_ = fmt->duration / (double)AV_TIME_BASE;
                        else if (st->duration != AV_NOPTS_VALUE)
                            duration_ = (double)st->duration * av_q2d(st->time_base);
                        containerName_ = (fmt->iformat && fmt->iformat->name) ? fmt->iformat->name : "";
                        const char *vcodec = avcodec_get_name(st->codecpar->codec_id);
                        videoCodec_ = vcodec ? vcodec : "";
                        
                        av_free(buffer);
                        sws_freeContext(sws);
                        success = true;
                        av_packet_unref(pkt);
                        break;
                    }
                }
            }
            av_packet_unref(pkt);
        }
        av_frame_free(&frame);
        av_frame_free(&rgb);
        av_packet_free(&pkt);
        avcodec_free_context(&c);
        avformat_close_input(&fmt);
        
        if (!success)
        {
            SetError("Failed to extract/encode frame");
        }
    }

    void OnOK() override
    {
        Napi::Env env = Env();
        Buffer<uint8_t> img = Buffer<uint8_t>::Copy(env, bmpData_.data(), bmpData_.size());
        Object obj = Object::New(env);
        obj.Set("width", Number::New(env, width_));
        obj.Set("height", Number::New(env, height_));
        obj.Set("duration", Number::New(env, duration_));
        obj.Set("format", String::New(env, containerName_));
        obj.Set("videoCodec", String::New(env, videoCodec_));
        obj.Set("image", img);
        deferred_.Resolve(obj);
    }

    void OnError(const Error &e) override
    {
        deferred_.Reject(e.Value());
    }

private:
    std::string path_;
    std::string fmtStr_;
    Promise::Deferred deferred_;
    std::vector<uint8_t> bmpData_;
    int width_;
    int height_;
    double duration_;
    std::string containerName_;
    std::string videoCodec_;
};

// getVideoInfo(path) -> { width, height, duration, format, videoCodec, image: Buffer }
// Extracts the first decoded video frame, container/video format info and duration.
// Currently only packs the frame as a 24-bit BMP (manual packing, no image encoder required).
Value GetVideoInfo(const CallbackInfo &info)
{
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString())
    {
        TypeError::New(env, "Expected a file path string").ThrowAsJavaScriptException();
        return env.Null();
    }
    std::string path = info[0].As<String>().Utf8Value();
    std::string fmtStr = "bmp";
    if (info.Length() >= 2 && info[1].IsString())
        fmtStr = info[1].As<String>().Utf8Value();
    // normalize
    for (auto &c : fmtStr)
        c = (char)tolower((unsigned char)c);

    // currently we only support BMP output (manual packing). Reject other formats early.
    if (!(fmtStr == "bmp" || fmtStr == "bmp24"))
    {
        TypeError::New(env, "Only 'bmp' / 'bmp24' output is supported").ThrowAsJavaScriptException();
        return env.Null();
    }

    Promise::Deferred deferred = Promise::Deferred::New(env);
    GetVideoInfoWorker *worker = new GetVideoInfoWorker(path, fmtStr, deferred);
    worker->Queue();
    return deferred.Promise();
}

// ===== ConvertToNTSilkTct Async Worker =====
class ConvertToNTSilkTctWorker : public AsyncWorker
{
public:
    ConvertToNTSilkTctWorker(const std::string &inPath, const std::string &outPath, Promise::Deferred deferred)
        : AsyncWorker(deferred.Env()), inPath_(inPath), outPath_(outPath), deferred_(deferred) {}

    void Execute() override
    {
        // 打开输入文件
        AVFormatContext *inFmt = nullptr;
        if (avformat_open_input(&inFmt, inPath_.c_str(), nullptr, nullptr) < 0)
        {
            SetError("Failed to open input");
            return;
        }
        if (avformat_find_stream_info(inFmt, nullptr) < 0)
        {
            avformat_close_input(&inFmt);
            SetError("Failed to find stream info");
            return;
        }

        // 只保留音频流,丢弃其它所有流
        for (unsigned i = 0; i < inFmt->nb_streams; ++i)
        {
            if (inFmt->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            {
                inFmt->streams[i]->discard = AVDISCARD_ALL;
            }
        }

        // 查找音频流
        int audioStream = -1;
        for (unsigned i = 0; i < inFmt->nb_streams; ++i)
        {
            if (inFmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                audioStream = i;
                break;
            }
        }
        if (audioStream < 0)
        {
            avformat_close_input(&inFmt);
            SetError("No audio stream");
            return;
        }

        // 初始化解码器
        AVStream *inSt = inFmt->streams[audioStream];
        const AVCodec *dec = avcodec_find_decoder(inSt->codecpar->codec_id);
        if (!dec)
        {
            avformat_close_input(&inFmt);
            SetError("Decoder not found");
            return;
        }
        AVCodecContext *decCtx = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(decCtx, inSt->codecpar);
        if (avcodec_open2(decCtx, dec, nullptr) < 0)
        {
            avcodec_free_context(&decCtx);
            avformat_close_input(&inFmt);
            SetError("Failed to open decoder");
            return;
        }

        // 确定目标采样率(选择最接近的支持采样率)
        static const int supported_rates[] = {48000, 44100, 32000, 24000, 16000, 12000, 8000};
        int input_rate = decCtx->sample_rate;
        int target_rate = 24000; // 默认24kHz

        // 如果输入采样率在支持列表中,直接使用
        for (int r : supported_rates)
        {
            if (input_rate == r)
            {
                target_rate = input_rate;
                break;
            }
        }
        // 否则选择最接近的支持采样率
        if (target_rate != input_rate)
        {
            int min_diff = abs(input_rate - supported_rates[0]);
            target_rate = supported_rates[0];
            for (int r : supported_rates)
            {
                int diff = abs(input_rate - r);
                if (diff < min_diff)
                {
                    min_diff = diff;
                    target_rate = r;
                }
            }
        }

        // 初始化重采样器(统一转换为单声道 S16 目标采样率)
        SwrContext *swr = swr_alloc();
        AVChannelLayout in_ch_layout = decCtx->ch_layout;
        AVChannelLayout out_ch_layout;
        av_channel_layout_default(&out_ch_layout, 1); // 单声道

        if (swr_alloc_set_opts2(&swr,
                                &out_ch_layout, AV_SAMPLE_FMT_S16, target_rate,
                                &in_ch_layout, decCtx->sample_fmt, decCtx->sample_rate,
                                0, nullptr) < 0 ||
            swr_init(swr) < 0)
        {
            if (swr)
                swr_free(&swr);
            avcodec_free_context(&decCtx);
            avformat_close_input(&inFmt);
            SetError("Failed to init resampler");
            return;
        }

        // 初始化编码器
        const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_NTSILK_S16LE);
        if (!enc)
        {
            swr_free(&swr);
            avcodec_free_context(&decCtx);
            avformat_close_input(&inFmt);
            SetError("Encoder (AV_CODEC_ID_NTSILK_S16LE) not found");
            return;
        }

        AVCodecContext *encCtx = avcodec_alloc_context3(enc);
        encCtx->sample_rate = target_rate;
        encCtx->sample_fmt = AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&encCtx->ch_layout, 1); // 单声道
        encCtx->time_base = {1, target_rate};

        if (avcodec_open2(encCtx, enc, nullptr) < 0)
        {
            avcodec_free_context(&encCtx);
            swr_free(&swr);
            avcodec_free_context(&decCtx);
            avformat_close_input(&inFmt);
            SetError("Failed to open encoder");
            return;
        }

        // 创建输出文件
        AVFormatContext *outFmt = nullptr;
        if (avformat_alloc_output_context2(&outFmt, nullptr, "ntsilk_s16le", outPath_.c_str()) < 0 || !outFmt)
        {
            avcodec_free_context(&encCtx);
            swr_free(&swr);
            avcodec_free_context(&decCtx);
            avformat_close_input(&inFmt);
            SetError("Failed to alloc output context");
            return;
        }

        // 创建输出流
        AVStream *outSt = avformat_new_stream(outFmt, nullptr);
        if (!outSt)
        {
            avformat_free_context(outFmt);
            avcodec_free_context(&encCtx);
            swr_free(&swr);
            avcodec_free_context(&decCtx);
            avformat_close_input(&inFmt);
            SetError("Failed to create output stream");
            return;
        }

        outSt->time_base = encCtx->time_base;
        if (avcodec_parameters_from_context(outSt->codecpar, encCtx) < 0)
        {
            avformat_free_context(outFmt);
            avcodec_free_context(&encCtx);
            swr_free(&swr);
            avcodec_free_context(&decCtx);
            avformat_close_input(&inFmt);
            SetError("Failed to copy encoder params");
            return;
        }

        // 打开输出文件
        if (!(outFmt->oformat->flags & AVFMT_NOFILE))
        {
            if (avio_open(&outFmt->pb, outPath_.c_str(), AVIO_FLAG_WRITE) < 0)
            {
                avformat_free_context(outFmt);
                avcodec_free_context(&encCtx);
                swr_free(&swr);
                avcodec_free_context(&decCtx);
                avformat_close_input(&inFmt);
                SetError("Failed to open output file");
                return;
            }
        }

        if (avformat_write_header(outFmt, nullptr) < 0)
        {
            if (!(outFmt->oformat->flags & AVFMT_NOFILE))
                avio_closep(&outFmt->pb);
            avformat_free_context(outFmt);
            avcodec_free_context(&encCtx);
            swr_free(&swr);
            avcodec_free_context(&decCtx);
            avformat_close_input(&inFmt);
            SetError("Failed to write header");
            return;
        }

        // 准备帧缓冲
        AVPacket *pkt = av_packet_alloc();
        AVFrame *decFrame = av_frame_alloc();
        AVFrame *resampledFrame = av_frame_alloc();

        // 编码器帧大小
        int frame_size = encCtx->frame_size > 0 ? encCtx->frame_size : 480;

        // 重采样输出缓冲区(用于累积采样直到够一个编码帧)
        std::vector<int16_t> sample_buffer;
        int64_t next_pts = 0;

        // 解码和重采样循环
        int ret;
        while ((ret = av_read_frame(inFmt, pkt)) >= 0)
        {
            if (pkt->stream_index != audioStream)
            {
                av_packet_unref(pkt);
                continue;
            }

            ret = avcodec_send_packet(decCtx, pkt);
            av_packet_unref(pkt);
            if (ret < 0)
                continue;

            while (avcodec_receive_frame(decCtx, decFrame) == 0)
            {
                // 计算重采样后的采样数
                int64_t delay = swr_get_delay(swr, decCtx->sample_rate);
                int64_t out_count = av_rescale_rnd(delay + decFrame->nb_samples, target_rate, decCtx->sample_rate, AV_ROUND_UP);

                // 分配重采样输出缓冲
                uint8_t *resampled_data = nullptr;
                int resampled_linesize = 0;
                av_samples_alloc(&resampled_data, &resampled_linesize, 1, out_count, AV_SAMPLE_FMT_S16, 0);

                // 执行重采样
                int converted_samples = swr_convert(swr, &resampled_data, out_count,
                                                    (const uint8_t **)decFrame->data, decFrame->nb_samples);

                if (converted_samples > 0)
                {
                    // 将重采样后的数据追加到缓冲区
                    int16_t *samples = (int16_t *)resampled_data;
                    sample_buffer.insert(sample_buffer.end(), samples, samples + converted_samples);
                }

                av_freep(&resampled_data);
                av_frame_unref(decFrame);

                // 当缓冲区有足够的采样时,送入编码器
                while ((int)sample_buffer.size() >= frame_size)
                {
                    // 准备编码帧
                    resampledFrame->nb_samples = frame_size;
                    resampledFrame->format = AV_SAMPLE_FMT_S16;
                    resampledFrame->sample_rate = target_rate;
                    av_channel_layout_default(&resampledFrame->ch_layout, 1);
                    av_frame_get_buffer(resampledFrame, 0);

                    memcpy(resampledFrame->data[0], sample_buffer.data(), frame_size * sizeof(int16_t));
                    resampledFrame->pts = next_pts;
                    next_pts += frame_size;

                    // 送入编码器
                    ret = avcodec_send_frame(encCtx, resampledFrame);
                    av_frame_unref(resampledFrame);

                    if (ret == 0)
                    {
                        AVPacket *outPkt = av_packet_alloc();
                        while (avcodec_receive_packet(encCtx, outPkt) == 0)
                        {
                            outPkt->stream_index = 0;
                            av_packet_rescale_ts(outPkt, encCtx->time_base, outSt->time_base);
                            av_interleaved_write_frame(outFmt, outPkt);
                            av_packet_unref(outPkt);
                        }
                        av_packet_free(&outPkt);
                    }

                    // 从缓冲区移除已编码的采样
                    sample_buffer.erase(sample_buffer.begin(), sample_buffer.begin() + frame_size);
                }
            }
        }

        // Flush 解码器
        avcodec_send_packet(decCtx, nullptr);
        while (avcodec_receive_frame(decCtx, decFrame) == 0)
        {
            int64_t delay = swr_get_delay(swr, decCtx->sample_rate);
            int64_t out_count = av_rescale_rnd(delay + decFrame->nb_samples, target_rate, decCtx->sample_rate, AV_ROUND_UP);

            uint8_t *resampled_data = nullptr;
            int resampled_linesize = 0;
            av_samples_alloc(&resampled_data, &resampled_linesize, 1, out_count, AV_SAMPLE_FMT_S16, 0);

            int converted_samples = swr_convert(swr, &resampled_data, out_count,
                                                (const uint8_t **)decFrame->data, decFrame->nb_samples);

            if (converted_samples > 0)
            {
                int16_t *samples = (int16_t *)resampled_data;
                sample_buffer.insert(sample_buffer.end(), samples, samples + converted_samples);
            }

            av_freep(&resampled_data);
            av_frame_unref(decFrame);

            while ((int)sample_buffer.size() >= frame_size)
            {
                resampledFrame->nb_samples = frame_size;
                resampledFrame->format = AV_SAMPLE_FMT_S16;
                resampledFrame->sample_rate = target_rate;
                av_channel_layout_default(&resampledFrame->ch_layout, 1);
                av_frame_get_buffer(resampledFrame, 0);

                memcpy(resampledFrame->data[0], sample_buffer.data(), frame_size * sizeof(int16_t));
                resampledFrame->pts = next_pts;
                next_pts += frame_size;

                ret = avcodec_send_frame(encCtx, resampledFrame);
                av_frame_unref(resampledFrame);

                if (ret == 0)
                {
                    AVPacket *outPkt = av_packet_alloc();
                    while (avcodec_receive_packet(encCtx, outPkt) == 0)
                    {
                        outPkt->stream_index = 0;
                        av_packet_rescale_ts(outPkt, encCtx->time_base, outSt->time_base);
                        av_interleaved_write_frame(outFmt, outPkt);
                        av_packet_unref(outPkt);
                    }
                    av_packet_free(&outPkt);
                }

                sample_buffer.erase(sample_buffer.begin(), sample_buffer.begin() + frame_size);
            }
        }

        // Flush 重采样器
        uint8_t *resampled_data = nullptr;
        int resampled_linesize = 0;
        int max_out_samples = frame_size * 2;
        av_samples_alloc(&resampled_data, &resampled_linesize, 1, max_out_samples, AV_SAMPLE_FMT_S16, 0);

        int converted_samples = swr_convert(swr, &resampled_data, max_out_samples, nullptr, 0);
        if (converted_samples > 0)
        {
            int16_t *samples = (int16_t *)resampled_data;
            sample_buffer.insert(sample_buffer.end(), samples, samples + converted_samples);
        }
        av_freep(&resampled_data);

        // 处理剩余的采样(包括不足一帧的部分)
        while (!sample_buffer.empty())
        {
            int samples_to_encode = std::min((int)sample_buffer.size(), frame_size);

            resampledFrame->nb_samples = samples_to_encode;
            resampledFrame->format = AV_SAMPLE_FMT_S16;
            resampledFrame->sample_rate = target_rate;
            av_channel_layout_default(&resampledFrame->ch_layout, 1);
            av_frame_get_buffer(resampledFrame, 0);

            memcpy(resampledFrame->data[0], sample_buffer.data(), samples_to_encode * sizeof(int16_t));
            resampledFrame->pts = next_pts;
            next_pts += samples_to_encode;

            ret = avcodec_send_frame(encCtx, resampledFrame);
            av_frame_unref(resampledFrame);

            if (ret == 0)
            {
                AVPacket *outPkt = av_packet_alloc();
                while (avcodec_receive_packet(encCtx, outPkt) == 0)
                {
                    outPkt->stream_index = 0;
                    av_packet_rescale_ts(outPkt, encCtx->time_base, outSt->time_base);
                    av_interleaved_write_frame(outFmt, outPkt);
                    av_packet_unref(outPkt);
                }
                av_packet_free(&outPkt);
            }

            sample_buffer.erase(sample_buffer.begin(), sample_buffer.begin() + samples_to_encode);
        }

        // Flush 编码器
        avcodec_send_frame(encCtx, nullptr);
        AVPacket *outPkt = av_packet_alloc();
        while (avcodec_receive_packet(encCtx, outPkt) == 0)
        {
            outPkt->stream_index = 0;
            av_packet_rescale_ts(outPkt, encCtx->time_base, outSt->time_base);
            av_interleaved_write_frame(outFmt, outPkt);
            av_packet_unref(outPkt);
        }
        av_packet_free(&outPkt);

        // 写入文件尾并清理
        av_write_trailer(outFmt);
        if (!(outFmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outFmt->pb);

        av_frame_free(&decFrame);
        av_frame_free(&resampledFrame);
        av_packet_free(&pkt);
        swr_free(&swr);
        avcodec_free_context(&encCtx);
        avformat_free_context(outFmt);
        avcodec_free_context(&decCtx);
        avformat_close_input(&inFmt);
    }

    void OnOK() override
    {
        deferred_.Resolve(Env().Undefined());
    }

    void OnError(const Error &e) override
    {
        deferred_.Reject(e.Value());
    }

private:
    std::string inPath_;
    std::string outPath_;
    Promise::Deferred deferred_;
};

Object Init(Env env, Object exports)
{
    exports.Set("getDuration", Function::New(env, GetDuration));
    exports.Set("getVideoInfo", Function::New(env, GetVideoInfo));
    exports.Set("convertToNTSilkTct", Function::New(env, ConvertToNTSilkTct));
    exports.Set("decodeAudioToPCM", Function::New(env, DecodeAudioToPCM));
    return exports;
}
// convertToNTSilkTct(inputPath, outputPath) -> void
Value ConvertToNTSilkTct(const CallbackInfo &info)
{
    Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString())
    {
        TypeError::New(env, "Expected input and output file path strings").ThrowAsJavaScriptException();
        return env.Null();
    }
    std::string inPath = info[0].As<String>().Utf8Value();
    std::string outPath = info[1].As<String>().Utf8Value();

    Promise::Deferred deferred = Promise::Deferred::New(env);
    ConvertToNTSilkTctWorker *worker = new ConvertToNTSilkTctWorker(inPath, outPath, deferred);
    worker->Queue();
    return deferred.Promise();
}

NODE_API_MODULE(ffmpegAddon, Init)
