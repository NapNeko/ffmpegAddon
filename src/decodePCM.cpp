#include "decodeAudio.h"
#include "audioPipeline.h"
#include <exception>

class DecodeAudioToPCMWorker : public AsyncWorker
{
public:
    DecodeAudioToPCMWorker(const std::string &inputPath, const std::string &outputPath, int targetSampleRate, Promise::Deferred deferred)
        : AsyncWorker(deferred.Env()), inputPath_(inputPath), outputPath_(outputPath), targetSampleRate_(targetSampleRate), deferred_(deferred), sampleRate_(0), channels_(0) {}

    void Execute() override
    {
        try
        {
            sampleRate_ = TranscodeAudio(inputPath_, outputPath_, "pcm", targetSampleRate_);
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
