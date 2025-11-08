// FFmpeg N-API addon (modular structure)
#include "ffmpegCommon.h"
#include "getDuration.h"
#include "decodeAudio.h"
#include "videoInfo.h"
#include "convertNTSilk.h"
#include "convertFile.h"

// Supported targets (intended to be enabled in FFmpeg build):
// - Containers (for cover & duration): avi, matroska (mkv), mov, mp4
// - Audio formats: mp3, amr, m4a(aac), ogg(vorbis), wav (pcm), flac
// The build script should enable only needed demuxers/decoders to keep size small.

Object Init(Env env, Object exports)
{
    exports.Set("getDuration", Function::New(env, GetDuration));
    exports.Set("getVideoInfo", Function::New(env, GetVideoInfo));
    exports.Set("convertToNTSilkTct", Function::New(env, ConvertToNTSilkTct));
    exports.Set("decodeAudioToPCM", Function::New(env, DecodeAudioToPCM));
    exports.Set("convertFile", Function::New(env, ConvertFile));
    return exports;
}

NODE_API_MODULE(ffmpegAddon, Init)
