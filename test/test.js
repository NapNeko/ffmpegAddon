const ffmpeg = require('../build/Release/ffmpegaddon');
console.log('Test Video MP4:', ffmpeg.getVideoInfo('E:/NewDevelop/Ffmpeg/test/test.mp4'));
console.log('Test Music MP3:', ffmpeg.getDuration('E:/NewDevelop/Ffmpeg/test/test.mp3'));