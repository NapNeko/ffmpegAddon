#include "convertNTSilk.h"
#include "audioPipeline.h"
#include <exception>

class ConvertToNTSilkTctWorker : public AsyncWorker
{
public:
    ConvertToNTSilkTctWorker(const std::string &inPath, const std::string &outPath, Promise::Deferred deferred)
        : AsyncWorker(deferred.Env()), inPath_(inPath), outPath_(outPath), deferred_(deferred) {}

    void Execute() override
    {
        try
        {
            TranscodeAudio(inPath_, outPath_, "ntsilk", 0);
        }
        catch (const std::exception &error)
        {
            SetError(error.what());
        }
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
