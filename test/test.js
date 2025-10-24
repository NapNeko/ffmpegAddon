const ffmpeg = require('../build/Release/ffmpegaddon');
const path = require('path');
const fs = require('fs');

const mp3_test = path.join(__dirname, 'test.mp3');
const mp4_test = path.join(__dirname, 'test.mp4');
const ntsilk_test = path.join(__dirname, 'test.ntsilk');
const ntsilk_out_test = path.join(__dirname, 'test_out.ntsilk');
const wav_out = path.join(__dirname, 'test_out.wav');

// Helper: create WAV header and concat with PCM buffer (16-bit PCM)
function createWavBuffer(pcmBuffer, sampleRate, channels, bitsPerSample = 16) {
  const byteRate = sampleRate * channels * bitsPerSample / 8;
  const blockAlign = channels * bitsPerSample / 8;
  const dataSize = pcmBuffer.length; // bytes
  const headerSize = 44;
  const buffer = Buffer.alloc(headerSize + dataSize);

  // RIFF chunk descriptor
  buffer.write('RIFF', 0); // ChunkID
  buffer.writeUInt32LE(headerSize + dataSize - 8, 4); // ChunkSize = 36 + SubChunk2Size
  buffer.write('WAVE', 8); // Format

  // fmt subchunk
  buffer.write('fmt ', 12); // Subchunk1ID
  buffer.writeUInt32LE(16, 16); // Subchunk1Size (16 for PCM)
  buffer.writeUInt16LE(1, 20); // AudioFormat 1 = PCM
  buffer.writeUInt16LE(channels, 22); // NumChannels
  buffer.writeUInt32LE(sampleRate, 24); // SampleRate
  buffer.writeUInt32LE(byteRate, 28); // ByteRate
  buffer.writeUInt16LE(blockAlign, 32); // BlockAlign
  buffer.writeUInt16LE(bitsPerSample, 34); // BitsPerSample

  // data subchunk
  buffer.write('data', 36); // Subchunk2ID
  buffer.writeUInt32LE(dataSize, 40); // Subchunk2Size

  // PCM data
  pcmBuffer.copy(buffer, headerSize);

  return buffer;
}

// 使用异步函数进行测试
async function runTests() {
  try {
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

    // 测试 getDuration (异步) - MP3
    console.log('测试 MP3 音频时长...');
    const mp3Duration = await ffmpeg.getDuration(mp3_test);
    console.log('MP3 时长:', mp3Duration, '秒');
    console.log();

    // 测试 getDuration (异步) - NTSILK
    console.log('测试 NTSILK 音频时长...');
    const ntsilkDuration = await ffmpeg.getDuration(ntsilk_test);
    console.log('NTSILK 时长:', ntsilkDuration, '秒');
    console.log();

    // 测试 convertToNTSilkTct (异步)
    console.log('测试 MP3 转 NTSILK...');
    await ffmpeg.convertToNTSilkTct(mp3_test, ntsilk_out_test);
    console.log('转换成功！');
    console.log();

    // 测试转换后的文件时长
    console.log('测试转换后的 NTSILK 时长...');
    const convertedDuration = await ffmpeg.getDuration(ntsilk_out_test);
    console.log('转换后 NTSILK 时长:', convertedDuration, '秒');
    console.log();

    // 测试 decodeAudioToPCM (异步)
    console.log('测试 NTSILK 解码到 PCM...');
    const decoded = await ffmpeg.decodeAudioToPCM(ntsilk_test);
    console.log('解码 SILK 元数据:', { 
      sampleRate: decoded.sampleRate, 
      channels: decoded.channels, 
      pcmLength: decoded.pcm.length 
    });

    // 验证并写入 WAV
    if (decoded && Buffer.isBuffer(decoded.pcm)) {
      const bitsPerSample = 16;
      const wavBuffer = createWavBuffer(
        decoded.pcm, 
        decoded.sampleRate || 24000, 
        decoded.channels || 1, 
        bitsPerSample
      );

      fs.writeFileSync(wav_out, wavBuffer);
      console.log('WAV 文件已写入:', wav_out);
    } else {
      console.error('decodeAudioToPCM 没有返回预期的带有 pcm Buffer 的对象');
    }

    console.log('\n=== 所有测试完成 ===');
  } catch (error) {
    console.error('测试出错:', error);
    process.exit(1);
  }
}

// 运行测试
runTests();