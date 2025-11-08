#ifndef CONVERTFILE_H
#define CONVERTFILE_H

#include "ffmpegCommon.h"

// convertFile(inputPath, outputPath, outputFormat) -> Promise
// 将任意文件转换为指定格式
// inputPath: 输入文件路径
// outputPath: 输出文件路径
// outputFormat: 输出格式 (例如: "mp3", "wav", "mp4", "avi" 等)
Value ConvertFile(const CallbackInfo &info);

#endif // CONVERTFILE_H
