// FFmpeg N-API addon (trimmed build expectations)
#include <napi.h>
#include <iostream>
extern "C" {
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

// Supported targets (intended to be enabled in FFmpeg build):
// - Containers (for cover & duration): avi, matroska (mkv), mov, mp4
// - Audio formats: mp3, amr, m4a(aac), ogg(vorbis), wav (pcm), flac
// The build script should enable only needed demuxers/decoders to keep size small.

// getDuration(path) -> number (seconds)
Value GetDuration(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        TypeError::New(env, "Expected a file path string").ThrowAsJavaScriptException();
        return env.Null();
    }
    std::string path = info[0].As<String>().Utf8Value();
    AVFormatContext* fmt = nullptr;
    int ret = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char buf[256];
        av_strerror(ret, buf, sizeof(buf));
        std::string msg = "Failed to open input: "; msg += buf;
        Error::New(env, msg).ThrowAsJavaScriptException();
        return env.Null();
    }
    if ((ret = avformat_find_stream_info(fmt, nullptr)) < 0) {
        avformat_close_input(&fmt);
        char buf[256]; av_strerror(ret, buf, sizeof(buf));
        std::string msg = "Failed to find stream info: "; msg += buf;
        Error::New(env, msg).ThrowAsJavaScriptException();
        return env.Null();
    }
    double duration = 0.0;
    if (fmt->duration != AV_NOPTS_VALUE) {
        duration = fmt->duration / (double)AV_TIME_BASE;
    } else {
        // fallback: find longest stream duration
        for (unsigned i = 0; i < fmt->nb_streams; ++i) {
            AVStream* st = fmt->streams[i];
            if (st->duration != AV_NOPTS_VALUE) {
                double d = (double)st->duration * av_q2d(st->time_base);
                if (d > duration) duration = d;
            }
        }
    }
    avformat_close_input(&fmt);
    return Number::New(env, duration);
}

// getVideoInfo(path) -> { width, height, duration, format, videoCodec, image: Buffer }
// Extracts the first decoded video frame, container/video format info and duration.
// Currently only packs the frame as a 24-bit BMP (manual packing, no image encoder required).
Value GetVideoInfo(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        TypeError::New(env, "Expected a file path string").ThrowAsJavaScriptException();
        return env.Null();
    }
    std::string path = info[0].As<String>().Utf8Value();
    std::string fmtStr = "bmp";
    if (info.Length() >= 2 && info[1].IsString()) fmtStr = info[1].As<String>().Utf8Value();
    // normalize
    for (auto &c : fmtStr) c = (char)tolower((unsigned char)c);

    // currently we only support BMP output (manual packing). Reject other formats early.
    if (!(fmtStr == "bmp" || fmtStr == "bmp24")) {
        TypeError::New(env, "Only 'bmp' / 'bmp24' output is supported").ThrowAsJavaScriptException();
        return env.Null();
    }

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) != 0) {
        Error::New(env, "Failed to open input").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        Error::New(env, "Failed to find stream info").ThrowAsJavaScriptException();
        return env.Null();
    }
    int vidStream = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { vidStream = i; break; }
    }
    if (vidStream < 0) { avformat_close_input(&fmt); Error::New(env, "No video stream").ThrowAsJavaScriptException(); return env.Null(); }
    AVStream* st = fmt->streams[vidStream];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) { avformat_close_input(&fmt); Error::New(env, "Decoder not found").ThrowAsJavaScriptException(); return env.Null(); }
    AVCodecContext* c = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(c, st->codecpar);
    if (avcodec_open2(c, dec, nullptr) < 0) { avcodec_free_context(&c); avformat_close_input(&fmt); Error::New(env, "Failed to open codec").ThrowAsJavaScriptException(); return env.Null(); }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgb = av_frame_alloc();
    struct SwsContext* sws = nullptr;
    int ret;
    while ((ret = av_read_frame(fmt, pkt)) >= 0) {
        if (pkt->stream_index == vidStream) {
            ret = avcodec_send_packet(c, pkt);
            if (ret < 0) { av_packet_unref(pkt); continue; }
            ret = avcodec_receive_frame(c, frame);
            if (ret == 0) {
                int w = frame->width, h = frame->height;
                int rgbLinesize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, w, h, 1);
                uint8_t* buffer = (uint8_t*)av_malloc(rgbLinesize);
                if (!buffer) {
                    // allocation failed
                    av_packet_unref(pkt);
                    break;
                }
                int afill = av_image_fill_arrays(rgb->data, rgb->linesize, buffer, AV_PIX_FMT_RGB24, w, h, 1);
                if (afill < 0) {
                    av_free(buffer);
                    av_packet_unref(pkt);
                    break;
                }
                sws = sws_getContext(w, h, (AVPixelFormat)frame->format, w, h, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!sws) {
                    av_free(buffer);
                    av_packet_unref(pkt);
                    break;
                }
                int scaled = sws_scale(sws, frame->data, frame->linesize, 0, h, rgb->data, rgb->linesize);
                if (scaled <= 0) {
                    av_free(buffer);
                    sws_freeContext(sws);
                    av_packet_unref(pkt);
                    break;
                }

                // if user requested BMP, build BMP manually (simple, no encoder needed)
                if (fmtStr == "bmp" || fmtStr == "bmp24") {
                    int pad = (4 - (w * 3) % 4) % 4;
                    uint32_t rowSize = w * 3 + pad;
                    uint32_t dataSize = rowSize * h;
                    uint32_t fileSize = 14 + 40 + dataSize;
                    std::vector<uint8_t> bmp;
                    bmp.resize(14 + 40 + dataSize);
                    uint8_t* p = bmp.data();
                    // BITMAPFILEHEADER
                    p[0] = 'B'; p[1] = 'M';
                    p[2] = (uint8_t)(fileSize & 0xFF);
                    p[3] = (uint8_t)((fileSize>>8)&0xFF);
                    p[4] = (uint8_t)((fileSize>>16)&0xFF);
                    p[5] = (uint8_t)((fileSize>>24)&0xFF);
                    p[6] = p[7] = p[8] = p[9] = 0;
                    uint32_t offset = 14 + 40;
                    p[10] = (uint8_t)(offset & 0xFF);
                    p[11] = (uint8_t)((offset>>8)&0xFF);
                    p[12] = (uint8_t)((offset>>16)&0xFF);
                    p[13] = (uint8_t)((offset>>24)&0xFF);
                    // BITMAPINFOHEADER
                    uint8_t* q = p + 14;
                    uint32_t biSize = 40;
                    q[0] = (uint8_t)(biSize & 0xFF);
                    q[1] = (uint8_t)((biSize>>8)&0xFF);
                    q[2] = (uint8_t)((biSize>>16)&0xFF);
                    q[3] = (uint8_t)((biSize>>24)&0xFF);
                    // width
                    q[4] = (uint8_t)(w & 0xFF);
                    q[5] = (uint8_t)((w>>8)&0xFF);
                    q[6] = (uint8_t)((w>>16)&0xFF);
                    q[7] = (uint8_t)((w>>24)&0xFF);
                    // height (positive for bottom-up)
                    q[8] = (uint8_t)(h & 0xFF);
                    q[9] = (uint8_t)((h>>8)&0xFF);
                    q[10] = (uint8_t)((h>>16)&0xFF);
                    q[11] = (uint8_t)((h>>24)&0xFF);
                    // planes
                    q[12] = 1; q[13] = 0;
                    // bit count
                    q[14] = 24; q[15] = 0;
                    // compression
                    q[16] = q[17] = q[18] = q[19] = 0;
                    // image size
                    q[20] = (uint8_t)(dataSize & 0xFF);
                    q[21] = (uint8_t)((dataSize>>8)&0xFF);
                    q[22] = (uint8_t)((dataSize>>16)&0xFF);
                    q[23] = (uint8_t)((dataSize>>24)&0xFF);
                    // xppm, yppm
                    uint32_t ppm = 2835; // 72 DPI
                    q[24] = (uint8_t)(ppm & 0xFF); q[25] = (uint8_t)((ppm>>8)&0xFF); q[26] = (uint8_t)((ppm>>16)&0xFF); q[27] = (uint8_t)((ppm>>24)&0xFF);
                    q[28] = (uint8_t)(ppm & 0xFF); q[29] = (uint8_t)((ppm>>8)&0xFF); q[30] = (uint8_t)((ppm>>16)&0xFF); q[31] = (uint8_t)((ppm>>24)&0xFF);
                    // clrUsed, clrImportant
                    q[32]=q[33]=q[34]=q[35]=0; q[36]=q[37]=q[38]=q[39]=0;
                    // pixel data: BMP is BGR bottom-up
                    uint8_t* dst = p + offset;
                    uint8_t* src = buffer; // rgb->data[0]
                    for (int y = h - 1; y >= 0; --y) {
                        uint8_t* rowDst = dst + (uint32_t)(h - 1 - y) * rowSize;
                        uint8_t* rowSrc = src + (uint32_t)y * w * 3;
                        for (int x = 0; x < w; ++x) {
                            // src: R G B
                            uint8_t r = rowSrc[x*3 + 0];
                            uint8_t g = rowSrc[x*3 + 1];
                            uint8_t b = rowSrc[x*3 + 2];
                            rowDst[x*3 + 0] = b;
                            rowDst[x*3 + 1] = g;
                            rowDst[x*3 + 2] = r;
                        }
                        // padding
                        for (uint32_t pz = 0; pz < (uint32_t)pad; ++pz) rowDst[w*3 + pz] = 0;
                    }
                    // prepare JS object
                    Buffer<uint8_t> img = Buffer<uint8_t>::Copy(env, bmp.data(), bmp.size());
                    Object obj = Object::New(env);
                    obj.Set("width", Number::New(env, w));
                    obj.Set("height", Number::New(env, h));
                    double duration = 0.0;
                    if (fmt->duration != AV_NOPTS_VALUE) duration = fmt->duration / (double)AV_TIME_BASE;
                    else if (st->duration != AV_NOPTS_VALUE) duration = (double)st->duration * av_q2d(st->time_base);
                    obj.Set("duration", Number::New(env, duration));
                    const char* containerName = fmt->iformat && fmt->iformat->name ? fmt->iformat->name : "";
                    obj.Set("format", String::New(env, containerName));
                    const char* vcodec = avcodec_get_name(st->codecpar->codec_id);
                    obj.Set("videoCodec", String::New(env, vcodec ? vcodec : ""));
                    obj.Set("image", img);
                    // cleanup
                    av_free(buffer);
                    sws_freeContext(sws);
                    av_frame_free(&frame);
                    av_frame_free(&rgb);
                    av_packet_free(&pkt);
                    avcodec_free_context(&c);
                    avformat_close_input(&fmt);
                    return obj;
                }
                // We only support BMP output now (no PNG/JPEG encoder dependency).
                // The BMP packaging was already done above and returned the result.
            }
        }
        av_packet_unref(pkt);
    }
    // cleanup on failure (we only reach here if no frame was decoded/returned)
    av_frame_free(&frame);
    av_frame_free(&rgb);
    av_packet_free(&pkt);
    avcodec_free_context(&c);
    avformat_close_input(&fmt);
    Error::New(env, "Failed to extract/encode frame").ThrowAsJavaScriptException();
    return env.Null();
}

// decodeAudioToPCM(path) -> { pcm: Buffer, sampleRate, channels }
// Decodes first audio stream to signed 16-bit little-endian PCM interleaved.
Value DecodeAudioToPCM(const CallbackInfo& info) {
    Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        TypeError::New(env, "Expected a file path string").ThrowAsJavaScriptException();
        return env.Null();
    }
    std::string path = info[0].As<String>().Utf8Value();
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) != 0) { Error::New(env, "Failed to open input").ThrowAsJavaScriptException(); return env.Null(); }
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); Error::New(env, "Failed to find stream info").ThrowAsJavaScriptException(); return env.Null(); }
    int audStream = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) { if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) { audStream = i; break; } }
    if (audStream < 0) { avformat_close_input(&fmt); Error::New(env, "No audio stream").ThrowAsJavaScriptException(); return env.Null(); }
    AVStream* st = fmt->streams[audStream];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) { avformat_close_input(&fmt); Error::New(env, "Decoder not found").ThrowAsJavaScriptException(); return env.Null(); }
    AVCodecContext* c = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(c, st->codecpar);
    if (avcodec_open2(c, dec, nullptr) < 0) { avcodec_free_context(&c); avformat_close_input(&fmt); Error::New(env, "Failed to open codec").ThrowAsJavaScriptException(); return env.Null(); }

    // use codecpar as a stable source of channels/sample_rate
    int src_channels = st->codecpar->ch_layout.nb_channels ? st->codecpar->ch_layout.nb_channels : 1;
    int src_sample_rate = st->codecpar->sample_rate;
    enum AVSampleFormat src_sample_fmt = (enum AVSampleFormat)st->codecpar->format;

    // Prepare input channel layout. If codecpar doesn't provide a valid layout, synthesize a default one.
    AVChannelLayout tmp_ch_layout;
    const AVChannelLayout* in_ch_layout = &st->codecpar->ch_layout;
    if (in_ch_layout->nb_channels == 0) {
        av_channel_layout_default(&tmp_ch_layout, src_channels);
        in_ch_layout = &tmp_ch_layout;
    }

    SwrContext* swr = nullptr;
    if (swr_alloc_set_opts2(&swr,
            in_ch_layout,                // out_ch_layout (we keep same layout)
            AV_SAMPLE_FMT_S16,           // out_sample_fmt
            src_sample_rate,             // out_sample_rate
            in_ch_layout,                // in_ch_layout
            src_sample_fmt,              // in_sample_fmt
            src_sample_rate,             // in_sample_rate
            0, nullptr) < 0 || !swr) {
        if (swr) swr_free(&swr);
        avcodec_free_context(&c);
        avformat_close_input(&fmt);
        Error::New(env, "Failed to init resampler").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (swr_init(swr) < 0) { swr_free(&swr); avcodec_free_context(&c); avformat_close_input(&fmt); Error::New(env, "Failed to init resampler").ThrowAsJavaScriptException(); return env.Null(); }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    std::vector<uint8_t> outbuf;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == audStream) {
            if (avcodec_send_packet(c, pkt) == 0) {
                while (avcodec_receive_frame(c, frame) == 0) {
                    // convert to s16
                    uint8_t** dst = nullptr;
                    int dst_nb_samples = swr_get_out_samples(swr, frame->nb_samples);
                    if (dst_nb_samples <= 0) dst_nb_samples = frame->nb_samples;
                    if (av_samples_alloc_array_and_samples(&dst, nullptr, src_channels, dst_nb_samples, AV_SAMPLE_FMT_S16, 0) < 0) { continue; }
                    int ret = swr_convert(swr, (uint8_t * const*)dst, dst_nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
                    if (ret < 0) { av_freep(&dst[0]); av_freep(&dst); continue; }
                    int nb = av_samples_get_buffer_size(nullptr, src_channels, ret, AV_SAMPLE_FMT_S16, 1);
                    size_t prev = outbuf.size(); outbuf.resize(prev + nb);
                    memcpy(outbuf.data() + prev, dst[0], nb);
                    av_freep(&dst[0]); av_freep(&dst);
                }
            }
        }
        av_packet_unref(pkt);
    }
    // cleanup
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    // capture sampleRate/channels before freeing context
    int sampleRate = c ? src_sample_rate : 0;
    int channels = c ? src_channels : 0;
    avcodec_free_context(&c);
    avformat_close_input(&fmt);

    Buffer<uint8_t> buf = Buffer<uint8_t>::Copy(env, outbuf.data(), outbuf.size());
    Object res = Object::New(env);
    res.Set("pcm", buf);
    res.Set("sampleRate", Number::New(env, sampleRate));
    res.Set("channels", Number::New(env, channels));
    return res;
}

Object Init(Env env, Object exports) {
    exports.Set("getDuration", Function::New(env, GetDuration));
    exports.Set("getVideoInfo", Function::New(env, GetVideoInfo));
    exports.Set("decodeAudioToPCM", Function::New(env, DecodeAudioToPCM));
    exports.Set("encodePCMToWAV", Function::New(env, [](const CallbackInfo& info)->Value{
        Env env = info.Env();
        Buffer<uint8_t> pcmBuf;
        int sampleRate = 0; int channels = 0;
        if (info.Length() == 1 && info[0].IsObject()) {
            Object o = info[0].As<Object>();
            if (!o.Has("pcm") || !o.Get("pcm").IsBuffer()) { TypeError::New(env, "Object must have pcm Buffer").ThrowAsJavaScriptException(); return env.Null(); }
            pcmBuf = o.Get("pcm").As<Buffer<uint8_t>>();
            if (o.Has("sampleRate") && o.Get("sampleRate").IsNumber()) sampleRate = o.Get("sampleRate").As<Number>().Int32Value();
            if (o.Has("channels") && o.Get("channels").IsNumber()) channels = o.Get("channels").As<Number>().Int32Value();
        } else if (info.Length() >= 3 && info[0].IsBuffer() && info[1].IsNumber() && info[2].IsNumber()) {
            pcmBuf = info[0].As<Buffer<uint8_t>>();
            sampleRate = info[1].As<Number>().Int32Value();
            channels = info[2].As<Number>().Int32Value();
        } else { TypeError::New(env, "Expected (pcmBuffer, sampleRate, channels) or {pcm, sampleRate, channels}").ThrowAsJavaScriptException(); return env.Null(); }
        if (sampleRate <= 0 || channels <= 0) { TypeError::New(env, "Invalid sampleRate or channels").ThrowAsJavaScriptException(); return env.Null(); }
        size_t pcmSize = pcmBuf.Length();
        // WAV header (RIFF) 44 bytes for PCM 16
        uint32_t byteRate = sampleRate * channels * 2; // 16-bit
        uint16_t blockAlign = channels * 2;
        uint32_t dataSize = (uint32_t)pcmSize;
        uint32_t riffSize = 36 + dataSize;
        std::vector<uint8_t> out;
        out.resize(44 + dataSize);
        // RIFF
        memcpy(out.data()+0, "RIFF", 4);
        out[4] = (uint8_t)(riffSize & 0xFF);
        out[5] = (uint8_t)((riffSize>>8)&0xFF);
        out[6] = (uint8_t)((riffSize>>16)&0xFF);
        out[7] = (uint8_t)((riffSize>>24)&0xFF);
        memcpy(out.data()+8, "WAVE", 4);
        // fmt chunk
        memcpy(out.data()+12, "fmt ", 4);
        uint32_t fmtLen = 16;
        out[16] = (uint8_t)(fmtLen & 0xFF);
        out[17] = (uint8_t)((fmtLen>>8)&0xFF);
        out[18] = (uint8_t)((fmtLen>>16)&0xFF);
        out[19] = (uint8_t)((fmtLen>>24)&0xFF);
        // audio format 1 = PCM
        out[20] = 1; out[21] = 0;
        // num channels
        out[22] = (uint8_t)(channels & 0xFF); out[23] = (uint8_t)((channels>>8)&0xFF);
        // sample rate
        out[24] = (uint8_t)(sampleRate & 0xFF);
        out[25] = (uint8_t)((sampleRate>>8)&0xFF);
        out[26] = (uint8_t)((sampleRate>>16)&0xFF);
        out[27] = (uint8_t)((sampleRate>>24)&0xFF);
        // byte rate
        out[28] = (uint8_t)(byteRate & 0xFF);
        out[29] = (uint8_t)((byteRate>>8)&0xFF);
        out[30] = (uint8_t)((byteRate>>16)&0xFF);
        out[31] = (uint8_t)((byteRate>>24)&0xFF);
        // block align
        out[32] = (uint8_t)(blockAlign & 0xFF);
        out[33] = (uint8_t)((blockAlign>>8)&0xFF);
        // bits per sample
        out[34] = 16; out[35] = 0;
        // data chunk
        memcpy(out.data()+36, "data", 4);
        out[40] = (uint8_t)(dataSize & 0xFF);
        out[41] = (uint8_t)((dataSize>>8)&0xFF);
        out[42] = (uint8_t)((dataSize>>16)&0xFF);
        out[43] = (uint8_t)((dataSize>>24)&0xFF);
        // copy pcm
        memcpy(out.data()+44, pcmBuf.Data(), dataSize);
        Buffer<uint8_t> res = Buffer<uint8_t>::Copy(env, out.data(), out.size());
        return res;
    }));
    return exports;
}

NODE_API_MODULE(ffmpegAddon, Init)
