#!/usr/bin/env bash
set -euo pipefail

# Minimal FFmpeg cross-build script for mingw-w64 on Linux
# Usage: ./scripts/build_ffmpeg_mingw.sh <install_prefix>

PREFIX="$(pwd)/buildout-w64"
if [ "$#" -ge 1 ]; then
  PREFIX="$1"
fi

echo "Install prefix: $PREFIX"

CONFIGURE_FLAGS=(
  --prefix="$PREFIX"
  --arch=x86_64
  --target-os=win64
  --disable-everything \
  --disable-programs \
  --disable-ffplay \
  --disable-ffprobe \
  --disable-doc \
  --disable-iconv \
  --disable-bzlib \
  --disable-libilbc \
  --disable-lzma \
  --disable-debug \
  --enable-small \
  --enable-stripping \
  --disable-avdevice \
  --disable-network \
  --enable-protocol=file \
  --enable-avformat \
  --enable-avcodec \
  --enable-avutil \
  --enable-swscale \
  --enable-swresample \
  --enable-filter=scale \
  --enable-filter=aresample \
  --enable-demuxer=mov \
  --enable-demuxer=mp4 \
  --enable-demuxer=matroska \
  --enable-demuxer=avi \
  --enable-demuxer=flv \
  --enable-demuxer=aac \
  --enable-demuxer=mp3 \
  --enable-demuxer=ogg \
  --enable-demuxer=wav \
  --enable-demuxer=flac \
  --enable-demuxer=h264 \
  --enable-demuxer=hevc \
  --enable-decoder=aac \
  --enable-decoder=mp3 \
  --enable-decoder=vorbis \
  --enable-decoder=flac \
  --enable-decoder=pcm_s16le \
  --enable-decoder=h264 \
  --enable-decoder=hevc \
  --enable-decoder=mjpeg \
  --enable-decoder=vp8 \
  --enable-decoder=vp9 \
  --enable-parser=h264 \
  --enable-parser=hevc \
  --enable-parser=aac \
  --enable-parser=mpegaudio \
  --enable-parser=vorbis \
  --enable-bsf=h264_mp4toannexb \
  --enable-bsf=hevc_mp4toannexb \
  --pkg-config-flags=--static
)

cd ffmpeg_src

# Ensure the cross compilers are available
if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
  echo "Error: x86_64-w64-mingw32-gcc not found in PATH. Install mingw-w64 toolchain."
  exit 2
fi

echo "Configuring FFmpeg for mingw cross-build..."
./configure "${CONFIGURE_FLAGS[@]}"

echo "Running make -j$(nproc || echo 4)"
make -j"$(nproc || echo 4)"

echo "Running make install"
make install

# Copy install to a normalized prefix (avoid weird relative paths)
mkdir -p "$PREFIX"
cp -r "$(pwd)/install/"* "$PREFIX" || true

echo
cat <<'EOF'
✅ Cross-build complete. Installed to: $PREFIX
Notes:
- This is a minimal cross-build for x86_64-w64-mingw32.
- If you need MSVC-compatible .lib files, you must build FFmpeg with MSVC.
- The produced libraries are GNU-style static (.a) and should be linkable by MinGW-built addons.
EOF

cd ..
