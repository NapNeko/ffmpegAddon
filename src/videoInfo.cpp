#include "videoInfo.h"
#include <iostream>
#include <memory>
#include <algorithm>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// 首帧封面用 JPEG 编码的质量。之前用全尺寸 PNG，一张 1080x1920 的首帧 PNG
// 可达 ~1.8MB；QQ 对视频封面有大小上限，超过后接收端会把整条视频渲染成
// 「视频已过期」（即使资源本身可下载）。同一帧 JPEG 只有几百 KB。
static const int THUMB_JPEG_QUALITY = 85;

class GetVideoInfoWorker : public Napi::AsyncWorker {
public:
    GetVideoInfoWorker(const std::string &path, Napi::Promise::Deferred deferred)
        : Napi::AsyncWorker(deferred.Env()), path_(path), deferred_(deferred),
          width_(0), height_(0), duration_(0.0),
          imgData_(nullptr), imgSize_(0) {}

    ~GetVideoInfoWorker() {
        if (imgData_) free(imgData_);
    }

    void Execute() override {
        AVFormatContext *fmt = nullptr;
        if (avformat_open_input(&fmt, path_.c_str(), nullptr, nullptr) != 0) {
            SetError("Failed to open input");
            return;
        }
        if (avformat_find_stream_info(fmt, nullptr) < 0) {
            avformat_close_input(&fmt);
            SetError("Failed to find stream info");
            return;
        }

        int vidStream = -1;
        for (unsigned i = 0; i < fmt->nb_streams; ++i) {
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                vidStream = i;
                break;
            }
        }
        if (vidStream < 0) {
            avformat_close_input(&fmt);
            SetError("No video stream");
            return;
        }

        AVStream *st = fmt->streams[vidStream];
        const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            avformat_close_input(&fmt);
            SetError("Decoder not found");
            return;
        }

        AVCodecContext *c = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(c, st->codecpar);
        if (avcodec_open2(c, dec, nullptr) < 0) {
            avcodec_free_context(&c);
            avformat_close_input(&fmt);
            SetError("Failed to open codec");
            return;
        }

        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        AVFrame *rgb = av_frame_alloc();
        SwsContext *sws = nullptr;
        uint8_t *buffer = nullptr;

        // 多块固定大小缓存
        const int CHUNK_SIZE = 64 * 1024; // 64KB
        struct Chunk { uint8_t *data; int offset; };
        std::vector<std::unique_ptr<Chunk>> chunks;

        auto writeFunc = [](void *context, void *data, int size) {
            auto *chunks = reinterpret_cast<std::vector<std::unique_ptr<Chunk>>*>(context);
            int written = 0;
            const int CHUNK_SIZE = 64 * 1024;
            while (written < size) {
                if (chunks->empty() || (*chunks)[chunks->size() - 1]->offset == CHUNK_SIZE) {
                    auto c = std::make_unique<Chunk>();
                    c->data = (uint8_t*)malloc(CHUNK_SIZE);
                    c->offset = 0;
                    chunks->push_back(std::move(c));
                }
                Chunk *cur = (*chunks)[chunks->size() - 1].get();
                int toCopy = std::min(size - written, CHUNK_SIZE - cur->offset);
                memcpy(cur->data + cur->offset, (uint8_t*)data + written, toCopy);
                cur->offset += toCopy;
                written += toCopy;
            }
        };

        bool success = false;
        while (av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == vidStream) {
                if (avcodec_send_packet(c, pkt) < 0) { av_packet_unref(pkt); continue; }
                if (avcodec_receive_frame(c, frame) == 0) {
                    int w = frame->width;
                    int h = frame->height;
                    width_ = w; height_ = h;

                    int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, w, h, 1);
                    buffer = (uint8_t*)av_malloc(bufSize);
                    if (!buffer) { av_packet_unref(pkt); break; }
                    av_image_fill_arrays(rgb->data, rgb->linesize, buffer, AV_PIX_FMT_RGB24, w, h, 1);

                    sws = sws_getContext(w, h, (AVPixelFormat)frame->format, w, h,
                                         AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (!sws) { av_free(buffer); buffer=nullptr; av_packet_unref(pkt); break; }

                    sws_scale(sws, frame->data, frame->linesize, 0, h, rgb->data, rgb->linesize);

                    // JPEG 写入多块缓存。rgb 以 align=1 填充（linesize == w*3，
                    // 行间无 padding），可直接喂给不带 stride 参数的 jpg 编码器。
                    stbi_write_jpg_to_func(writeFunc, &chunks, w, h, 3, rgb->data[0], THUMB_JPEG_QUALITY);

                    // 视频时长
                    if (fmt->duration != AV_NOPTS_VALUE)
                        duration_ = fmt->duration / (double)AV_TIME_BASE;
                    else if (st->duration != AV_NOPTS_VALUE)
                        duration_ = (double)st->duration * av_q2d(st->time_base);

                    // 编码器名
                    const char *vcodec = avcodec_get_name(st->codecpar->codec_id);
                    videoCodec_ = vcodec ? vcodec : "";

                    success = true;
                    av_packet_unref(pkt);
                    break;
                }
            }
            av_packet_unref(pkt);
        }

        // 合并所有块为连续内存
        if (success) {
            size_t totalSize = 0;
            for (auto &c : chunks) totalSize += c->offset;
            imgData_ = (uint8_t*)malloc(totalSize);
            imgSize_ = totalSize;
            size_t dstOffset = 0;
            for (auto &c : chunks) {
                memcpy(imgData_ + dstOffset, c->data, c->offset);
                dstOffset += c->offset;
                free(c->data);
            }
            chunks.clear();
        }

        // 清理
        if (buffer) av_free(buffer);
        if (sws) sws_freeContext(sws);
        av_frame_free(&frame);
        av_frame_free(&rgb);
        av_packet_free(&pkt);
        avcodec_free_context(&c);
        avformat_close_input(&fmt);

        if (!success) SetError("Failed to extract/encode frame");
    }

    void OnOK() override {
        Napi::Env env = Env();
        // 创建内部Buffer并复制数据
        Napi::Buffer<uint8_t> img = Napi::Buffer<uint8_t>::New(env, imgSize_);
        memcpy(img.Data(), imgData_, imgSize_);
        free(imgData_);
        imgData_ = nullptr;

        Napi::Object obj = Napi::Object::New(env);
        obj.Set("width", Napi::Number::New(env, width_));
        obj.Set("height", Napi::Number::New(env, height_));
        obj.Set("duration", Napi::Number::New(env, duration_));
        obj.Set("format", "jpg");
        obj.Set("videoCodec", videoCodec_);
        obj.Set("image", img);
        deferred_.Resolve(obj);
    }

    void OnError(const Napi::Error &e) override { deferred_.Reject(e.Value()); }

private:
    std::string path_;
    Napi::Promise::Deferred deferred_;
    uint8_t *imgData_;
    size_t imgSize_;
    int width_;
    int height_;
    double duration_;
    std::string videoCodec_;
};

// Node.js 接口
Napi::Value GetVideoInfo(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected a file path string").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string path = info[0].As<Napi::String>().Utf8Value();
    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);
    GetVideoInfoWorker *worker = new GetVideoInfoWorker(path, deferred);
    worker->Queue();
    return deferred.Promise();
}
