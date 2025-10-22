#!/usr/bin/env bash
set -euo pipefail

PREFIX=${1:-}
if [ -z "$PREFIX" ]; then
  echo "Usage: $0 <install-prefix>" >&2
  exit 2
fi

echo "Install prefix: $PREFIX"

CONFIGURE_FLAGS=(
  --prefix="$PREFIX" \
  --toolchain=msvc \
  --arch=x86_64 \
  --target-os=win64 \
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
  --disable-avfilter \
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
  --enable-decoder=silk \
  --enable-encoder=silk \
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


echo "Configuring FFmpeg for mingw cross-build..."
./configure "${CONFIGURE_FLAGS[@]}"

echo "Running make -j$(nproc || echo 4)"
make -j"$(nproc || echo 4)"

echo "Running make install"
make install

mkdir -p "$PREFIX"
cp -r "$(pwd)/install/"* "$PREFIX" || true

cd ..
