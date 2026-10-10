const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const { mkdtempSync, readFileSync, writeFileSync, rmSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { test } = require('node:test');

const addonPath = path.resolve(process.env.FFMPEG_ADDON_PATH || path.join(__dirname, '../build/Release/ffmpegAddon.node'));

function wave(sampleRate, sampleCount, channels = 1) {
    const result = Buffer.alloc(44 + sampleCount * channels * 2);
    result.write('RIFF', 0);
    result.writeUInt32LE(result.length - 8, 4);
    result.write('WAVEfmt ', 8);
    result.writeUInt32LE(16, 16);
    result.writeUInt16LE(1, 20);
    result.writeUInt16LE(channels, 22);
    result.writeUInt32LE(sampleRate, 24);
    result.writeUInt32LE(sampleRate * channels * 2, 28);
    result.writeUInt16LE(channels * 2, 32);
    result.writeUInt16LE(16, 34);
    result.write('data', 36);
    result.writeUInt32LE(result.length - 44, 40);
    for (let sample = 0; sample < sampleCount; sample += 1) {
        for (let channel = 0; channel < channels; channel += 1) {
            result.writeInt16LE(Math.round(Math.sin(sample * Math.PI * 880 / sampleRate) * 10000), 44 + (sample * channels + channel) * 2);
        }
    }
    return result;
}

async function probe(mode) {
    const addon = require(addonPath);
    const directory = mkdtempSync(path.join(tmpdir(), 'napcat audio 中文 # '));
    const input = path.join(directory, 'input.wav');
    const inputWave = wave(48000, 48000);
    writeFileSync(input, inputWave);
    try {
        if (mode === 'pcm') {
            assert.equal(await addon.getDuration(input), 1);
            for (const [sampleRate, channels] of [[48000, 1], [44100, 2], [8000, 1]]) {
                writeFileSync(input, wave(sampleRate, sampleRate, channels));
                const output = path.join(directory, 'output.pcm');
                const metadata = await addon.decodeAudioToPCM(input, output, 24000);
                assert.equal(metadata.sampleRate, 24000);
                assert.equal(readFileSync(output).length, 24000 * 2, 'resampling must preserve the final samples');
            }
        } else if (mode === 'silk') {
            for (const milliseconds of [13, 20, 1000, 1013]) {
                writeFileSync(input, wave(48000, milliseconds * 48));
                const output = path.join(directory, 'output.silk');
                await addon.convertToNTSilkTct(input, output);
                const encoded = readFileSync(output);
                assert.equal(encoded.subarray(0, 10).toString('hex'), '02232153494c4b5f5633');
                const expected = Math.ceil(milliseconds / 20) * 0.02;
                assert.ok(Math.abs(await addon.getDuration(output) - expected) < 0.000001);
                const decoded = path.join(directory, 'decoded.pcm');
                await addon.decodeAudioToPCM(output, decoded, 24000);
                assert.equal(readFileSync(decoded).length, Math.round(expected * 24000) * 2);
                writeFileSync(output, encoded.subarray(0, encoded.length - 1));
                await assert.rejects(addon.getDuration(output), /Invalid data/);
                await assert.rejects(addon.decodeAudioToPCM(output, decoded), /Invalid data/);
            }
        } else if (mode === 'errors') {
            const output = path.join(directory, 'output.wav');
            writeFileSync(output, 'keep');
            await assert.rejects(addon.decodeAudioToPCM(path.join(directory, 'missing.wav'), output), /Failed to open input/);
            assert.equal(readFileSync(output, 'utf8'), 'keep');
            await assert.rejects(addon.decodeAudioToFmt(input, output, 'unknown'), /Unsupported output format/);
            await assert.rejects(addon.decodeAudioToFmt(input, output, 'amr', 48000), /sample rate is not supported/);
            await assert.rejects(addon.decodeAudioToPCM(input, output, -1), /Invalid audio sample rate/);
            await assert.rejects(addon.decodeAudioToPCM(input, path.join(directory, 'missing', 'output.pcm')), /Failed to open output file/);
            writeFileSync(input, 'invalid audio');
            await assert.rejects(addon.getDuration(input), /Failed to open input/);
        } else if (mode === 'concurrent') {
            await Promise.all(Array.from({ length: 12 }, async (_, index) => {
                const format = ['wav', 'mp3', 'ntsilk'][index % 3];
                const output = path.join(directory, `${index}.${format}`);
                if (format === 'ntsilk') await addon.convertToNTSilkTct(input, output);
                else await addon.decodeAudioToFmt(input, output, format);
                const duration = await addon.getDuration(output);
                assert.ok(duration >= 1 && duration < 1.1, String(duration));
            }));
        } else {
            const output = path.join(directory, `output.${mode}`);
            const metadata = await addon.decodeAudioToFmt(input, output, mode);
            assert.equal(metadata.channels, 1);
            assert.equal(metadata.sampleRate, mode === 'amr' ? 8000 : mode === 'spx' ? 32000 : 48000);
            const duration = await addon.getDuration(output);
            assert.ok(duration >= 0.9 && duration <= 1.2, `unexpected ${mode} duration: ${duration}`);
            const decoded = path.join(directory, 'decoded.pcm');
            await addon.decodeAudioToPCM(output, decoded, 48000);
            const pcm = readFileSync(decoded);
            assert.ok(pcm.length >= 0.9 * 48000 * 2 && pcm.length <= 1.2 * 48000 * 2, `${mode} decoded ${pcm.length} bytes`);
            assert.ok(pcm.some(byte => byte !== 0), 'decoded audio must not be silence');
            if (mode === 'wav' || mode === 'flac') assert.deepEqual(pcm, inputWave.subarray(44));
        }
    } finally {
        rmSync(directory, { recursive: true, force: true });
    }
}

if (process.argv[2] === '--probe') {
    probe(process.argv[3]).catch(error => {
        console.error(error);
        process.exitCode = 1;
    });
} else {
    for (const mode of ['pcm', 'silk', 'mp3', 'amr', 'wma', 'm4a', 'spx', 'ogg', 'wav', 'flac', 'errors', 'concurrent']) {
        test(`native audio: ${mode}`, () => {
            const result = spawnSync(process.execPath, [__filename, '--probe', mode], {
                encoding: 'utf8',
                timeout: 30000,
                windowsHide: true,
                env: { ...process.env, FFMPEG_ADDON_PATH: addonPath },
            });
            assert.ifError(result.error);
            assert.equal(result.signal, null, result.stderr);
            assert.equal(result.status, 0, result.stderr);
        });
    }
}
