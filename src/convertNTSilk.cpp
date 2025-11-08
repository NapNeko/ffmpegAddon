#include "convertNTSilk.h"
#include <iostream>
#include <algorithm>

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
                if (av_samples_alloc(&resampled_data, &resampled_linesize, 1, out_count, AV_SAMPLE_FMT_S16, 0) >= 0)
                {
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
                }
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
            if (av_samples_alloc(&resampled_data, &resampled_linesize, 1, out_count, AV_SAMPLE_FMT_S16, 0) >= 0)
            {
                int converted_samples = swr_convert(swr, &resampled_data, out_count,
                                                    (const uint8_t **)decFrame->data, decFrame->nb_samples);

                if (converted_samples > 0)
                {
                    int16_t *samples = (int16_t *)resampled_data;
                    sample_buffer.insert(sample_buffer.end(), samples, samples + converted_samples);
                }

                av_freep(&resampled_data);
            }
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
        {
            uint8_t *resampled_data = nullptr;
            int resampled_linesize = 0;
            int max_out_samples = frame_size * 2;
            if (av_samples_alloc(&resampled_data, &resampled_linesize, 1, max_out_samples, AV_SAMPLE_FMT_S16, 0) >= 0)
            {
                int converted_samples = swr_convert(swr, &resampled_data, max_out_samples, nullptr, 0);
                if (converted_samples > 0)
                {
                    int16_t *samples = (int16_t *)resampled_data;
                    sample_buffer.insert(sample_buffer.end(), samples, samples + converted_samples);
                }
                av_freep(&resampled_data);
            }
        }

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
