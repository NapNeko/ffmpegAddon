#include "videoInfo.h"
#include <iostream>

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

