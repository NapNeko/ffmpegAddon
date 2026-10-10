# FFmpeg Node Addon

NapCat 使用的 FFmpeg 原生模块，支持：

- 音频解码、重采样及格式转换。
- SILK 编解码和音频时长读取。
- 视频信息及 JPEG / PNG 缩略图。

## 构建与测试

构建工作流支持 Windows x64、Linux x64 / arm64、macOS x64 / arm64。FFmpeg 版本由工作流的 `FFMPEG_COMMIT` 固定，并应用 `patches/` 中的补丁。

构建后运行 `npm test`。测试已有模块时通过 `FFMPEG_ADDON_PATH` 指定路径；视频素材的生成方法见 [fixtures](test/fixtures/README.md)。

## Thanks

[ntsilk](https://github.com/ntsilk/ntsilk)
