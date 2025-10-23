const ffmpeg = require('../build/Release/ffmpegaddon');
const path = require('path');
const mp3_test = path.join(__dirname, 'test.mp3');
const mp4_test = path.join(__dirname, 'test.mp4');
const ntsilk_test = path.join(__dirname, 'test.ntsilk');
console.log('Test Video MP4:', ffmpeg.getVideoInfo(mp4_test));
console.log('Test Music MP3:', ffmpeg.getDuration(mp3_test));
console.log('Test Music MP3:', ffmpeg.getDuration(ntsilk_test));
//console.log('Test Music SILK:', ffmpeg.decodeAudioToPCM(ntsilk_test));