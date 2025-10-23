console.log('Test Music MP3:', ffmpeg.getDuration('E:/NewDevelop/Ffmpeg/test/test.mp3'));console.log('等待40秒，可附加调试器...');
setTimeout(() => {
	console.log('Test Music SILK:', ffmpeg.decodeAudioToPCM('E:/NewDevelop/Ffmpeg/test/test.silk'));
	// console.log('Test Music MP3:', ffmpeg.getDuration('E:/NewDevelop/Ffmpeg/test/test.mp3'));
}, 15000);
