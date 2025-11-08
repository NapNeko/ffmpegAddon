#!/usr/bin/env bash
set -euo pipefail

PREFIX=${1:-}
if [ -z "$PREFIX" ]; then
  echo "Usage: $0 <install-prefix>" >&2
  exit 2
fi

echo "Building FFmpeg into prefix: $PREFIX"

if [ ! -d ffmpeg_src ]; then
  echo "ffmpeg_src directory not found; please clone FFmpeg into ffmpeg_src" >&2
  exit 3
fi

cd ffmpeg_src

EXTRA_CFLAGS="-fPIC -DPIC -I$PREFIX/include"
EXTRA_CXXFLAGS="-fPIC -DPIC -I$PREFIX/include"
EXTRA_LDFLAGS="-fPIC -L$PREFIX/lib"

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

echo "Configuring FFmpeg with position-independent code (PIC) enabled"

./configure \
  --prefix="$PREFIX" \
  --disable-everything \
  --disable-programs \
  --disable-ffplay \
  --disable-ffprobe \
  --disable-doc \
  --disable-iconv \
  --disable-libilbc \
  --disable-lzma \
  --disable-bzlib \
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
  --enable-demuxer=amr \
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
  --enable-decoder=amrnb \
  --enable-decoder=amrwb \
  --enable-decoder=h264 \
  --enable-encoder=aac \
  --enable-encoder=flac \
  --enable-encoder=pcm_s16le \
  --enable-encoder=libopencore_amrnb \
  --enable-encoder=libspeex \
  --enable-encoder=wmav1 \
  --enable-encoder=wmav2 \
  --enable-libmp3lame \
  --enable-encoder=libmp3lame \
  --enable-decoder=mp3float \
  --enable-muxer=mp3 \
  --enable-muxer=mp4 \
  --enable-muxer=ogg \
  --enable-muxer=wav \
  --enable-muxer=flac \
  --enable-muxer=amr \
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
  --enable-parser=amr \
  --enable-bsf=h264_mp4toannexb \
  --enable-bsf=hevc_mp4toannexb \
  --disable-x86asm \
  --disable-inline-asm \
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
