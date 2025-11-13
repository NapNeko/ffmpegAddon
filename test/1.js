const ffmpeg = require('../build/Release/ffmpegaddon');
const path = require('path');
const fs = require('fs');


const mp4_test = path.join(__dirname, 'test.mp4');
// 使用异步函数进行测试
async function runTests() {
    console.log('=== 开始异步测试 ===\n');

    // 测试 getVideoInfo (异步)
    console.log('测试 MP4 视频信息...');
    const videoInfo = await ffmpeg.getVideoInfo(mp4_test);
    console.log('视频信息:', {
      width: videoInfo.width,
      height: videoInfo.height,
      duration: videoInfo.duration,
      format: videoInfo.format,
      videoCodec: videoInfo.videoCodec,
      imageSize: videoInfo.image.length
    });
    console.log();
}

// 运行测试
runTests();