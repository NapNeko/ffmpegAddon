const ffmpeg = require('../build/Release/ffmpegaddon');
const path = require('path');
const fs = require('fs');

const mp3_test = path.join(__dirname, 'test.mp3');
const mp4_test = path.join(__dirname, 'test.mp4');
const ntsilk_test = path.join(__dirname, 'test.ntsilk');
const ntsilk_out_test = path.join(__dirname, 'test_out.ntsilk');
const wav_out = path.join(__dirname, 'test_out.wav');

console.log('Test Video MP4:', ffmpeg.getVideoInfo(mp4_test));
console.log('Test Music MP3:', ffmpeg.getDuration(mp3_test));
console.log('Test Music MP3:', ffmpeg.getDuration(ntsilk_test));
console.log('Test Music SILK convert:', ffmpeg.convertToNTSilkTct(mp3_test, ntsilk_out_test));
console.log('Test Music MP3:', ffmpeg.getDuration(ntsilk_out_test));

// decode to PCM
const decoded = ffmpeg.decodeAudioToPCM(ntsilk_test);
console.log('Decoded SILK meta:', { sampleRate: decoded.sampleRate, channels: decoded.channels, pcmLength: decoded.pcm.length });

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

// Validate and write WAV
if (decoded && Buffer.isBuffer(decoded.pcm)) {
  // 如果 decode 返回的 PCM 不是 16-bit，你需要先将其转换为 Int16. 下面假设已经是 Int16LE bytes。
  const bitsPerSample = 16;
  const wavBuffer = createWavBuffer(decoded.pcm, decoded.sampleRate || 24000, decoded.channels || 1, bitsPerSample);

  fs.writeFileSync(wav_out, wavBuffer);
  console.log('WAV written to', wav_out);
} else {
  console.error('decodeAudioToPCM did not return expected object with pcm Buffer');
}