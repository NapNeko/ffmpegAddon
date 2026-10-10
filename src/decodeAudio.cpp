#include "decodeAudio.h"
#include "audioPipeline.h"
#include <exception>

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
        try
        {
            sampleRate_ = TranscodeAudio(inputPath_, outputPath_, targetFormat_, targetSampleRate_);
            channels_ = 1;
        }
        catch (const std::exception &error)
        {
            SetError(error.what());
        }
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
