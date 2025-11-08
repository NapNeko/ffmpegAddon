const addon = require('../build/Release/ffmpegAddon.node');
const path = require('path');

async function testDecodeAudioToFmt() {
    console.log('Testing DecodeAudioToFmt function...\n');

    const inputFile = path.join("C:\\Users\\nanaeo\\Documents\\Tencent Files\\3161592748\\nt_qq\\nt_data\\Ptt\\2025-11\\Ori\\4c7cf94487bac47dee65eb47c93a6fbe.amr");
    const formats = ['mp3', 'wav', 'flac', 'ogg', 'm4a'];

    for (const format of formats) {
        const outputFile = path.join(__dirname, `test_output.${format}`);
        
        try {
            console.log(`Converting to ${format.toUpperCase()}...`);
            
            // Test with auto sample rate detection
            const result = await addon.decodeAudioToFmt(inputFile, outputFile, format);
            
            console.log(`✓ Success!`);
            console.log(`  - Format: ${result.format}`);
            console.log(`  - Sample Rate: ${result.sampleRate} Hz`);
            console.log(`  - Channels: ${result.channels}`);
            console.log(`  - Output: ${outputFile}\n`);
        } catch (error) {
            console.error(`✗ Failed to convert to ${format}:`, error.message, '\n');
        }
    }

    // Test with specific sample rate
    console.log('Testing with specific sample rate (16000 Hz)...');
    const outputFile = path.join(__dirname, 'test_output_16k.wav');
    
    try {
        const result = await addon.decodeAudioToFmt(inputFile, outputFile, 'wav', 16000);
        console.log(`✓ Success!`);
        console.log(`  - Format: ${result.format}`);
        console.log(`  - Sample Rate: ${result.sampleRate} Hz`);
        console.log(`  - Channels: ${result.channels}`);
        console.log(`  - Output: ${outputFile}\n`);
    } catch (error) {
        console.error(`✗ Failed:`, error.message, '\n');
    }

    console.log('All tests completed!');
}

testDecodeAudioToFmt().catch(console.error);
