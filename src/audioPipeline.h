#pragma once

#include <string>

int TranscodeAudio(const std::string &inputPath, const std::string &outputPath,
                   const std::string &format, int requestedSampleRate);
