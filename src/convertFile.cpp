#include "convertFile.h"
#include <iostream>
#include <vector>

extern "C" {
#include <libavutil/audio_fifo.h>
}

// ===== ConvertFile Async Worker =====
class ConvertFileWorker : public AsyncWorker
{
public:
    ConvertFileWorker(const std::string &inputPath, const std::string &outputPath, const std::string &outputFormat, Promise::Deferred deferred)
        : AsyncWorker(deferred.Env()), inputPath_(inputPath), outputPath_(outputPath), outputFormat_(outputFormat), deferred_(deferred) {}

    void Execute() override
    {
        AVFormatContext *inFmt = nullptr;
        AVFormatContext *outFmt = nullptr;
        AVCodecContext **decCtxArray = nullptr;
        AVCodecContext **encCtxArray = nullptr;
        SwrContext **swrArray = nullptr;
        SwsContext **swsArray = nullptr;
        AVAudioFifo **audioFifoArray = nullptr;
        unsigned int streamCount = 0;

        // 打开输入文件
        if (avformat_open_input(&inFmt, inputPath_.c_str(), nullptr, nullptr) < 0)
        {
            SetError("Failed to open input file");
            return;
        }

        if (avformat_find_stream_info(inFmt, nullptr) < 0)
        {
            avformat_close_input(&inFmt);
            SetError("Failed to find stream info");
            return;
        }

        // 创建输出上下文
        if (avformat_alloc_output_context2(&outFmt, nullptr, outputFormat_.c_str(), outputPath_.c_str()) < 0)
        {
            avformat_close_input(&inFmt);
            SetError("Failed to allocate output context");
            return;
        }

        // 为每个流分配解码器和编码器上下文数组
        streamCount = inFmt->nb_streams;
        decCtxArray = new (std::nothrow) AVCodecContext *[streamCount]();
        encCtxArray = new (std::nothrow) AVCodecContext *[streamCount]();
        swrArray = new (std::nothrow) SwrContext *[streamCount]();
        swsArray = new (std::nothrow) SwsContext *[streamCount]();
        audioFifoArray = new (std::nothrow) AVAudioFifo *[streamCount]();
        
        if (!decCtxArray || !encCtxArray || !swrArray || !swsArray || !audioFifoArray)
        {
            SetError("Failed to allocate context arrays");
            goto cleanup;
        }

        // 为每个输入流创建对应的输出流
        for (unsigned int i = 0; i < streamCount; i++)
        {
            AVStream *inStream = inFmt->streams[i];
            AVCodecParameters *inCodecPar = inStream->codecpar;

            // 跳过非音频流
            if (inCodecPar->codec_type != AVMEDIA_TYPE_AUDIO)
            {
                continue;
            }

            // 查找解码器
            const AVCodec *decoder = avcodec_find_decoder(inCodecPar->codec_id);
            if (!decoder)
            {
                continue; // 跳过无法解码的流
            }

            // 创建解码器上下文
            decCtxArray[i] = avcodec_alloc_context3(decoder);
            if (!decCtxArray[i])
            {
                continue;
            }

            if (avcodec_parameters_to_context(decCtxArray[i], inCodecPar) < 0)
            {
                avcodec_free_context(&decCtxArray[i]);
                continue;
            }

            if (avcodec_open2(decCtxArray[i], decoder, nullptr) < 0)
            {
                avcodec_free_context(&decCtxArray[i]);
                continue;
            }

            // 创建输出流
            AVStream *outStream = avformat_new_stream(outFmt, nullptr);
            if (!outStream)
            {
                avcodec_free_context(&decCtxArray[i]);
                continue;
            }

            // 根据媒体类型选择编码器
            const AVCodec *encoder = nullptr;
            if (inCodecPar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                encoder = avcodec_find_encoder(outFmt->oformat->video_codec);
            }
            else if (inCodecPar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                encoder = avcodec_find_encoder(outFmt->oformat->audio_codec);
            }
            else if (inCodecPar->codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                encoder = avcodec_find_encoder(outFmt->oformat->subtitle_codec);
            }

            if (!encoder)
            {
                // 如果找不到编码器，尝试直接复制流
                if (avcodec_parameters_copy(outStream->codecpar, inCodecPar) < 0)
                {
                    avcodec_free_context(&decCtxArray[i]);
                    continue;
                }
                outStream->time_base = inStream->time_base;
                continue;
            }

            // 创建编码器上下文
            encCtxArray[i] = avcodec_alloc_context3(encoder);
            if (!encCtxArray[i])
            {
                avcodec_free_context(&decCtxArray[i]);
                continue;
            }

            // 配置编码器参数
            if (inCodecPar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                encCtxArray[i]->height = decCtxArray[i]->height;
                encCtxArray[i]->width = decCtxArray[i]->width;
                encCtxArray[i]->sample_aspect_ratio = decCtxArray[i]->sample_aspect_ratio;

                // 选择编码器支持的像素格式
                if (encoder->pix_fmts)
                {
                    encCtxArray[i]->pix_fmt = encoder->pix_fmts[0];
                }
                else
                {
                    encCtxArray[i]->pix_fmt = decCtxArray[i]->pix_fmt;
                }

                encCtxArray[i]->time_base = av_inv_q(av_guess_frame_rate(inFmt, inStream, nullptr));
                encCtxArray[i]->framerate = av_guess_frame_rate(inFmt, inStream, nullptr);

                // 如果需要像素格式转换，初始化 SwsContext
                if (encCtxArray[i]->pix_fmt != decCtxArray[i]->pix_fmt)
                {
                    swsArray[i] = sws_getContext(
                        decCtxArray[i]->width, decCtxArray[i]->height, decCtxArray[i]->pix_fmt,
                        encCtxArray[i]->width, encCtxArray[i]->height, encCtxArray[i]->pix_fmt,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                }
            }
            else if (inCodecPar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                encCtxArray[i]->sample_rate = decCtxArray[i]->sample_rate;
                encCtxArray[i]->ch_layout = decCtxArray[i]->ch_layout;

                // 选择编码器支持的采样格式
                if (encoder->sample_fmts)
                {
                    encCtxArray[i]->sample_fmt = encoder->sample_fmts[0];
                }
                else
                {
                    encCtxArray[i]->sample_fmt = decCtxArray[i]->sample_fmt;
                }

                encCtxArray[i]->time_base = {1, encCtxArray[i]->sample_rate};
                
                // 设置编码器的 frame_size（对于 MP3 等编码器很重要）
                if (encoder->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
                {
                    encCtxArray[i]->frame_size = 0; // 可变帧大小
                }
                // 如果编码器没有指定 frame_size，使用默认值
                // libmp3lame 会在 avcodec_open2 后自动设置为 1152

                // 如果需要音频重采样，初始化 SwrContext
                if (encCtxArray[i]->sample_fmt != decCtxArray[i]->sample_fmt ||
                    encCtxArray[i]->sample_rate != decCtxArray[i]->sample_rate ||
                    av_channel_layout_compare(&encCtxArray[i]->ch_layout, &decCtxArray[i]->ch_layout) != 0)
                {
                    swr_alloc_set_opts2(&swrArray[i],
                                        &encCtxArray[i]->ch_layout, encCtxArray[i]->sample_fmt, encCtxArray[i]->sample_rate,
                                        &decCtxArray[i]->ch_layout, decCtxArray[i]->sample_fmt, decCtxArray[i]->sample_rate,
                                        0, nullptr);
                    if (swrArray[i])
                    {
                        swr_init(swrArray[i]);
                    }
                }
            }

            // 打开编码器
            if (avcodec_open2(encCtxArray[i], encoder, nullptr) < 0)
            {
                avcodec_free_context(&encCtxArray[i]);
                avcodec_free_context(&decCtxArray[i]);
                continue;
            }

            // 为音频流创建 FIFO 缓冲区（用于处理帧大小对齐）
            if (inCodecPar->codec_type == AVMEDIA_TYPE_AUDIO && encCtxArray[i]->frame_size > 0)
            {
                audioFifoArray[i] = av_audio_fifo_alloc(encCtxArray[i]->sample_fmt,
                                                        encCtxArray[i]->ch_layout.nb_channels,
                                                        encCtxArray[i]->frame_size);
            }

            // 复制编码器参数到输出流
            if (avcodec_parameters_from_context(outStream->codecpar, encCtxArray[i]) < 0)
            {
                avcodec_free_context(&encCtxArray[i]);
                avcodec_free_context(&decCtxArray[i]);
                continue;
            }

            outStream->time_base = encCtxArray[i]->time_base;
        }

        // 打开输出文件
        if (!(outFmt->oformat->flags & AVFMT_NOFILE))
        {
            if (avio_open(&outFmt->pb, outputPath_.c_str(), AVIO_FLAG_WRITE) < 0)
            {
                SetError("Failed to open output file");
                goto cleanup;
            }
        }

        // 写入文件头
        if (avformat_write_header(outFmt, nullptr) < 0)
        {
            SetError("Failed to write header");
            goto cleanup;
        }

        // 处理每个数据包
        {
            AVPacket *packet = av_packet_alloc();
            AVFrame *frame = av_frame_alloc();
            AVFrame *convertedFrame = av_frame_alloc();

            while (av_read_frame(inFmt, packet) >= 0)
            {
                unsigned int streamIndex = packet->stream_index;
                AVStream *inStream = inFmt->streams[streamIndex];

                // 跳过非音频流的数据包
                if (inStream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                {
                    av_packet_unref(packet);
                    continue;
                }

                AVStream *outStream = outFmt->streams[streamIndex];

                // 如果该流没有编码器（直接复制流）
                if (!encCtxArray[streamIndex])
                {
                    av_packet_rescale_ts(packet, inStream->time_base, outStream->time_base);
                    packet->stream_index = streamIndex;
                    av_interleaved_write_frame(outFmt, packet);
                    av_packet_unref(packet);
                    continue;
                }

                // 解码
                if (avcodec_send_packet(decCtxArray[streamIndex], packet) == 0)
                {
                    while (avcodec_receive_frame(decCtxArray[streamIndex], frame) == 0)
                    {
                        AVFrame *frameToEncode = frame;

                        // 视频格式转换
                        if (swsArray[streamIndex])
                        {
                            convertedFrame->format = encCtxArray[streamIndex]->pix_fmt;
                            convertedFrame->width = encCtxArray[streamIndex]->width;
                            convertedFrame->height = encCtxArray[streamIndex]->height;
                            av_frame_get_buffer(convertedFrame, 0);

                            sws_scale(swsArray[streamIndex],
                                      frame->data, frame->linesize, 0, frame->height,
                                      convertedFrame->data, convertedFrame->linesize);

                            convertedFrame->pts = frame->pts;
                            frameToEncode = convertedFrame;
                        }

                        // 音频重采样
                        if (swrArray[streamIndex])
                        {
                            int out_samples = av_rescale_rnd(
                                swr_get_delay(swrArray[streamIndex], decCtxArray[streamIndex]->sample_rate) + frame->nb_samples,
                                encCtxArray[streamIndex]->sample_rate,
                                decCtxArray[streamIndex]->sample_rate,
                                AV_ROUND_UP);

                            convertedFrame->format = encCtxArray[streamIndex]->sample_fmt;
                            convertedFrame->ch_layout = encCtxArray[streamIndex]->ch_layout;
                            convertedFrame->sample_rate = encCtxArray[streamIndex]->sample_rate;
                            convertedFrame->nb_samples = out_samples;
                            av_frame_get_buffer(convertedFrame, 0);

                            int converted = swr_convert(swrArray[streamIndex],
                                                        convertedFrame->data, out_samples,
                                                        (const uint8_t **)frame->data, frame->nb_samples);

                            if (converted > 0)
                            {
                                convertedFrame->nb_samples = converted;
                                convertedFrame->pts = av_rescale_q(frame->pts, decCtxArray[streamIndex]->time_base, encCtxArray[streamIndex]->time_base);
                                frameToEncode = convertedFrame;
                            }
                        }

                        // 如果使用 FIFO 缓冲区（用于固定帧大小的编码器）
                        if (audioFifoArray[streamIndex])
                        {
                            // 将重采样后的音频数据写入 FIFO
                            av_audio_fifo_write(audioFifoArray[streamIndex], (void **)frameToEncode->data, frameToEncode->nb_samples);

                            // 当 FIFO 中有足够的样本时，编码完整的帧
                            while (av_audio_fifo_size(audioFifoArray[streamIndex]) >= encCtxArray[streamIndex]->frame_size)
                            {
                                AVFrame *fifoFrame = av_frame_alloc();
                                fifoFrame->nb_samples = encCtxArray[streamIndex]->frame_size;
                                fifoFrame->format = encCtxArray[streamIndex]->sample_fmt;
                                fifoFrame->ch_layout = encCtxArray[streamIndex]->ch_layout;
                                fifoFrame->sample_rate = encCtxArray[streamIndex]->sample_rate;
                                av_frame_get_buffer(fifoFrame, 0);

                                av_audio_fifo_read(audioFifoArray[streamIndex], (void **)fifoFrame->data, encCtxArray[streamIndex]->frame_size);
                                fifoFrame->pts = av_rescale_q(frameToEncode->pts, encCtxArray[streamIndex]->time_base, encCtxArray[streamIndex]->time_base);

                                if (avcodec_send_frame(encCtxArray[streamIndex], fifoFrame) == 0)
                                {
                                    AVPacket *outPacket = av_packet_alloc();
                                    while (avcodec_receive_packet(encCtxArray[streamIndex], outPacket) == 0)
                                    {
                                        outPacket->stream_index = streamIndex;
                                        av_packet_rescale_ts(outPacket, encCtxArray[streamIndex]->time_base, outStream->time_base);
                                        av_interleaved_write_frame(outFmt, outPacket);
                                        av_packet_unref(outPacket);
                                    }
                                    av_packet_free(&outPacket);
                                }
                                av_frame_free(&fifoFrame);
                            }
                        }
                        else
                        {
                            // 直接编码（无需 FIFO 缓冲）
                            if (avcodec_send_frame(encCtxArray[streamIndex], frameToEncode) == 0)
                            {
                                AVPacket *outPacket = av_packet_alloc();
                                while (avcodec_receive_packet(encCtxArray[streamIndex], outPacket) == 0)
                                {
                                    outPacket->stream_index = streamIndex;
                                    av_packet_rescale_ts(outPacket, encCtxArray[streamIndex]->time_base, outStream->time_base);
                                    av_interleaved_write_frame(outFmt, outPacket);
                                    av_packet_unref(outPacket);
                                }
                                av_packet_free(&outPacket);
                            }
                        }

                        av_frame_unref(convertedFrame);
                        av_frame_unref(frame);
                    }
                }

                av_packet_unref(packet);
            }

            // Flush 解码器和编码器
            for (unsigned int i = 0; i < streamCount; i++)
            {
                if (decCtxArray[i] && encCtxArray[i])
                {
                    // Flush 解码器
                    avcodec_send_packet(decCtxArray[i], nullptr);
                    while (avcodec_receive_frame(decCtxArray[i], frame) == 0)
                    {
                        // 如果使用 FIFO，将剩余帧写入 FIFO
                        if (audioFifoArray[i])
                        {
                            av_audio_fifo_write(audioFifoArray[i], (void **)frame->data, frame->nb_samples);
                        }
                        else
                        {
                            avcodec_send_frame(encCtxArray[i], frame);
                            AVPacket *outPacket = av_packet_alloc();
                            while (avcodec_receive_packet(encCtxArray[i], outPacket) == 0)
                            {
                                outPacket->stream_index = i;
                                av_packet_rescale_ts(outPacket, encCtxArray[i]->time_base, outFmt->streams[i]->time_base);
                                av_interleaved_write_frame(outFmt, outPacket);
                                av_packet_unref(outPacket);
                            }
                            av_packet_free(&outPacket);
                        }
                        av_frame_unref(frame);
                    }

                    // Flush FIFO 缓冲区中的剩余样本
                    if (audioFifoArray[i])
                    {
                        while (av_audio_fifo_size(audioFifoArray[i]) > 0)
                        {
                            int fifo_size = av_audio_fifo_size(audioFifoArray[i]);
                            int samples_to_read = (fifo_size >= encCtxArray[i]->frame_size) ? encCtxArray[i]->frame_size : fifo_size;
                            
                            AVFrame *fifoFrame = av_frame_alloc();
                            fifoFrame->nb_samples = samples_to_read;
                            fifoFrame->format = encCtxArray[i]->sample_fmt;
                            fifoFrame->ch_layout = encCtxArray[i]->ch_layout;
                            fifoFrame->sample_rate = encCtxArray[i]->sample_rate;
                            av_frame_get_buffer(fifoFrame, 0);

                            av_audio_fifo_read(audioFifoArray[i], (void **)fifoFrame->data, samples_to_read);

                            if (avcodec_send_frame(encCtxArray[i], fifoFrame) == 0)
                            {
                                AVPacket *outPacket = av_packet_alloc();
                                while (avcodec_receive_packet(encCtxArray[i], outPacket) == 0)
                                {
                                    outPacket->stream_index = i;
                                    av_packet_rescale_ts(outPacket, encCtxArray[i]->time_base, outFmt->streams[i]->time_base);
                                    av_interleaved_write_frame(outFmt, outPacket);
                                    av_packet_unref(outPacket);
                                }
                                av_packet_free(&outPacket);
                            }
                            av_frame_free(&fifoFrame);
                        }
                    }

                    // Flush 编码器
                    avcodec_send_frame(encCtxArray[i], nullptr);
                    AVPacket *outPacket = av_packet_alloc();
                    while (avcodec_receive_packet(encCtxArray[i], outPacket) == 0)
                    {
                        outPacket->stream_index = i;
                        av_packet_rescale_ts(outPacket, encCtxArray[i]->time_base, outFmt->streams[i]->time_base);
                        av_interleaved_write_frame(outFmt, outPacket);
                        av_packet_unref(outPacket);
                    }
                    av_packet_free(&outPacket);
                }
            }

            av_frame_free(&convertedFrame);
            av_frame_free(&frame);
            av_packet_free(&packet);
        }

        // 写入文件尾
        av_write_trailer(outFmt);

    cleanup:
        // 清理资源
        if (audioFifoArray)
        {
            for (unsigned int i = 0; i < streamCount; i++)
            {
                if (audioFifoArray[i])
                    av_audio_fifo_free(audioFifoArray[i]);
            }
            delete[] audioFifoArray;
        }

        if (swsArray)
        {
            for (unsigned int i = 0; i < streamCount; i++)
            {
                if (swsArray[i])
                    sws_freeContext(swsArray[i]);
            }
            delete[] swsArray;
        }

        if (swrArray)
        {
            for (unsigned int i = 0; i < streamCount; i++)
            {
                if (swrArray[i])
                    swr_free(&swrArray[i]);
            }
            delete[] swrArray;
        }

        if (encCtxArray)
        {
            for (unsigned int i = 0; i < streamCount; i++)
            {
                if (encCtxArray[i])
                    avcodec_free_context(&encCtxArray[i]);
            }
            delete[] encCtxArray;
        }

        if (decCtxArray)
        {
            for (unsigned int i = 0; i < streamCount; i++)
            {
                if (decCtxArray[i])
                    avcodec_free_context(&decCtxArray[i]);
            }
            delete[] decCtxArray;
        }

        if (outFmt)
        {
            if (!(outFmt->oformat->flags & AVFMT_NOFILE))
                avio_closep(&outFmt->pb);
            avformat_free_context(outFmt);
        }

        if (inFmt)
            avformat_close_input(&inFmt);
    }

    void OnOK() override
    {
        Napi::Env env = Env();
        Object res = Object::New(env);
        res.Set("success", Boolean::New(env, true));
        deferred_.Resolve(res);
    }

    void OnError(const Error &e) override
    {
        deferred_.Reject(e.Value());
    }

private:
    std::string inputPath_;
    std::string outputPath_;
    std::string outputFormat_;
    Promise::Deferred deferred_;
};

Value ConvertFile(const CallbackInfo &info)
{
    Env env = info.Env();

    if (info.Length() < 3 || !info[0].IsString() || !info[1].IsString() || !info[2].IsString())
    {
        TypeError::New(env, "Expected inputPath (string), outputPath (string), and outputFormat (string)").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string inputPath = info[0].As<String>().Utf8Value();
    std::string outputPath = info[1].As<String>().Utf8Value();
    std::string outputFormat = info[2].As<String>().Utf8Value();

    Promise::Deferred deferred = Promise::Deferred::New(env);
    ConvertFileWorker *worker = new ConvertFileWorker(inputPath, outputPath, outputFormat, deferred);
    worker->Queue();
    return deferred.Promise();
}
