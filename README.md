# FFmpeg Node Addon
This project is a Node.js addon that provides bindings to the FFmpeg multimedia framework. 
It allows you to leverage FFmpeg's powerful capabilities directly from your Node.js applications.

## 支持功能
- [x] audio2silk. 音频(ogg mp3 wav acc flac)转silk格式
- [x] silk2pcm. silk格式转pcm
- [x] getVideoInfo. 获取视频信息
- [x] getAudioDuration. 获取音频时长 不支持Silk格式

## 构建与视频回归测试

CI 的四个平台统一使用 FFmpeg `release/7.1` 的固定提交
`7107f093d7dafaf60f30a350eba8926aded39952`，随后应用 `patches/` 中的补丁。
更新上游版本时，修改构建工作流中的 `FFMPEG_COMMIT`。

构建完成后执行 `npm run test:video`，检查非对齐宽度的视频在串行和并发提取封面时不会终止进程，且 JPEG 尺寸、颜色正确。测试素材生成命令见 [test/fixtures/README.md](test/fixtures/README.md)。

## Thanks
[ntsilk](https://github.com/ntsilk/ntsilk)
