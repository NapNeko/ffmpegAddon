# Video thumbnail regression fixture

`red-blue-360x480.mp4` is a synthetic, one-frame H.264 video: red in the top half and blue in the bottom half. Its width exercises the RGB24 SIMD tail that overwrites a tightly allocated destination. The colors also detect missing row-stride handling when encoding a padded frame as JPEG.

`black-360x480.mp4` is the 1,953-byte minimal reproducer for the same heap corruption.

Generate it with:

```sh
ffmpeg -f lavfi -i 'color=c=red:size=360x480:rate=1:duration=1,drawbox=x=0:y=240:w=360:h=240:color=blue:t=fill' -c:v libx264 -preset ultrafast -pix_fmt yuv420p -an red-blue-360x480.mp4
ffmpeg -f lavfi -i 'color=c=black:size=360x480:rate=1:duration=1' -c:v libx264 -preset ultrafast -pix_fmt yuv420p -an black-360x480.mp4
```

After building the addon, run `npm run test:video`. To test an existing binary, set `FFMPEG_ADDON_PATH` to its absolute path. Tests run repeated serial and concurrent calls in child processes, so a native crash is reported as a test failure.
