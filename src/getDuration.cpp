#include "getDuration.h"

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

