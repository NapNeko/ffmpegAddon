const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const path = require('node:path');
const { test } = require('node:test');
const jpeg = require('jpeg-js');
const { PNG } = require('pngjs');

const addonPath = path.resolve(process.env.FFMPEG_ADDON_PATH || path.join(__dirname, '../build/Release/ffmpegAddon.node'));
const fixtureName = process.argv[4] || 'red-blue-360x480.mp4';
const fixturePath = path.join(__dirname, 'fixtures', fixtureName);
const imageFormat = process.argv[5] || 'jpg';

function checkInfo(info) {
    assert.equal(info.width, 360);
    assert.equal(info.height, 480);
    assert.equal(info.duration, 1);
    assert.equal(info.videoCodec, 'h264');
    assert.equal(info.format, imageFormat);
    assert.ok(Buffer.isBuffer(info.image));
    assert.ok(info.image.length > 0 && info.image.length < 1024 * 1024);
}

function checkPixels(image) {
    const decoded = imageFormat === 'png' ? PNG.sync.read(image) : jpeg.decode(image, { useTArray: true });
    assert.equal(decoded.width, 360);
    assert.equal(decoded.height, 480);
    for (const column of [16, 180, 344]) {
        const top = (80 * decoded.width + column) * 4;
        const bottom = (400 * decoded.width + column) * 4;
        if (fixtureName === 'black-360x480.mp4') {
            for (const offset of [top, bottom]) {
                assert.ok(decoded.data[offset] < 20 && decoded.data[offset + 1] < 20 && decoded.data[offset + 2] < 20);
            }
        } else {
            assert.ok(decoded.data[top] > 200 && decoded.data[top + 1] < 50 && decoded.data[top + 2] < 50, 'top half must remain red');
            assert.ok(decoded.data[bottom] < 50 && decoded.data[bottom + 1] < 50 && decoded.data[bottom + 2] > 200, 'bottom half must remain blue');
        }
    }
}

async function probe(concurrency) {
    const addon = require(addonPath);
    assert.throws(() => addon.getVideoInfo(fixturePath, 'invalid'), /jpg or png/);
    await assert.rejects(addon.getVideoInfo(path.join(__dirname, 'missing.mp4')), /Failed to open input/);
    let completed = 0;
    for (let batch = 0; batch < 100; batch += 1) {
        const results = await Promise.all(Array.from({ length: concurrency }, () => addon.getVideoInfo(fixturePath, imageFormat)));
        for (const result of results) checkInfo(result);
        completed += results.length;
        if (batch === 0 || batch === 99) checkPixels(results[0].image);
    }
    process.stdout.write(JSON.stringify({ completed }));
}

if (process.argv[2] === '--probe') {
    probe(Number(process.argv[3])).catch((error) => {
        console.error(error);
        process.exitCode = 1;
    });
} else {
    for (const fixture of ['black-360x480.mp4', 'red-blue-360x480.mp4']) {
        for (const concurrency of [1, 4]) {
          for (const format of ['jpg', 'png']) {
            test(`extracts ${fixture} thumbnails with concurrency ${concurrency}`, () => {
                const result = spawnSync(process.execPath, [__filename, '--probe', String(concurrency), fixture, format], {
                    encoding: 'utf8',
                    timeout: 60000,
                    windowsHide: true,
                    env: { ...process.env, FFMPEG_ADDON_PATH: addonPath },
                });
                assert.ifError(result.error);
                assert.equal(result.signal, null, result.stderr);
                assert.equal(result.status, 0, `native probe exited with ${result.status}: ${result.stderr}`);
                assert.deepEqual(JSON.parse(result.stdout), { completed: 100 * concurrency });
            });
          }
        }
    }
}
