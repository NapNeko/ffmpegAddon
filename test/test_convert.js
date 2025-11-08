const addon = require('../build/Release/ffmpegAddon.node');
const path = require('path');

async function testConvertFile() {
    console.log('Testing convertFile function...\n');

    // 测试用例 1: 将 MP3 转换为 WAV
    try {
        console.log('Test 1: Converting MP3 to WAV');
        const inputFile = path.join(__dirname, 'test.mp3');
        const outputFile = path.join(__dirname, 'test_convert_output.wav');
        
        const result = await addon.convertFile(inputFile, outputFile, 'wav');
        console.log('✓ MP3 to WAV conversion successful:', result);
    } catch (error) {
        console.error('✗ MP3 to WAV conversion failed:', error.message);
    }
}

// 运行测试
testConvertFile().catch(console.error);
