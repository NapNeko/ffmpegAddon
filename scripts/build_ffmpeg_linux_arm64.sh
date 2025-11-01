#!/usr/bin/env bash
set -euo pipefail

PREFIX=${1:-}
if [ -z "$PREFIX" ]; then
  echo "Usage: $0 <install-prefix>" >&2
  exit 2
fi

echo "Install prefix: $PREFIX"

cd ffmpeg_src

EXTRA_CFLAGS="-fPIC -DPIC"
EXTRA_CXXFLAGS="-fPIC -DPIC"
EXTRA_LDFLAGS="-fPIC"

./configure \
  --prefix="$PREFIX" \
  --disable-everything \
  --disable-programs \
  --disable-ffplay \
  --disable-ffprobe \
  --disable-doc \
  --disable-iconv \
  --disable-libilbc \
  --disable-bzlib \
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
  --enable-demuxer=ntsilk_skp_s16le \
  --enable-muxer=ntsilk_skp_s16le \
  --enable-demuxer=ntsilk_tct_s16le \
  --enable-muxer=ntsilk_tct_s16le \
  --enable-decoder=ntsilk_skp_s16le \
  --enable-encoder=ntsilk_skp_s16le \
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
  --disable-asm \
  --pkg-config-flags=--static \
  --extra-cflags="$EXTRA_CFLAGS" \
  --extra-cxxflags="$EXTRA_CXXFLAGS" \
  --extra-ldflags="$EXTRA_LDFLAGS" \
  --enable-pic

if command -v nproc >/dev/null 2>&1; then
  JOBS=$(nproc)
elif [[ "$(uname)" == "Darwin" ]]; then
  JOBS=$(sysctl -n hw.ncpu)
else
  JOBS=1
fi

echo "Running make -j${JOBS}"
make -j${JOBS}
make install