(# FFmpeg Node Addon

This repository builds a Node.js native addon that links against FFmpeg. The project can build FFmpeg from source (into `buildout`) and then compile the Node addon using `cmake-js`.

## What the CI does

The included GitHub Actions workflow (`.github/workflows/build-ffmpeg-and-addon.yml`) will:

- Clone `release/7.1` from the FFmpeg official repository into `ffmpeg_src`.
- Copy and apply any `patches/*.patch` files from this repository to `ffmpeg_src` using `git am`.
- Configure and build FFmpeg (static) into `buildout`.
- Install Node dependencies and run `npx cmake-js build` to produce the `.node` addon.

## Reproducing locally

Linux (Ubuntu/Debian):

```bash
git clone --depth 1 --branch release/7.1 https://git.ffmpeg.org/ffmpeg.git ffmpeg_src
cp patches/*.patch ffmpeg_src/ || true
cd ffmpeg_src
git am --3way --committer-date-is-author-date --signoff --keep-cr *.patch || (git am --abort; exit 1)
./configure --prefix="$(pwd)/../buildout" --enable-static --disable-shared --disable-programs --disable-doc --disable-debug --disable-ffplay --disable-ffprobe --enable-small --enable-avcodec --enable-avformat --enable-swresample --enable-swscale --enable-avutil
make -j$(nproc)
make install
cd ..
npm ci
npx cmake-js build
```

Windows (MSYS2 + Visual Studio):

- Start an MSYS2 shell from the "x64 Native Tools Command Prompt for VS" so the MSVC environment is active.
- Run:

```bash
./scripts/build_ffmpeg_msvc.sh "$(pwd)/buildout"
npm ci
npx cmake-js build
```

Notes:

- Building FFmpeg from source is resource- and time-intensive. Consider running the CI workflow manually (`workflow_dispatch`) or building only as needed.
- Patches in `patches/` are applied with `git am --3way`. If they fail, inspect the patch and FFmpeg history; the workflow will abort on failures.

## License

This repository carries an MIT license for the addon glue. FFmpeg is licensed under its own licenses as provided in `ffmpeg_src`.
)
