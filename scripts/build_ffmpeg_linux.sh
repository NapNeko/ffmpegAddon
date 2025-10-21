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

# 清理旧构建（确保不混入非 -fPIC 的对象）
make distclean || true

# 设置额外编译标志，强制启用位置无关代码
EXTRA_CFLAGS="-fPIC -DPIC"
EXTRA_CXXFLAGS="-fPIC -DPIC"
EXTRA_LDFLAGS="-fPIC"

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
  --disable-x86asm \
  --disable-inline-asm \
  --pkg-config-flags=--static \
  --extra-cflags="$EXTRA_CFLAGS" \
  --extra-cxxflags="$EXTRA_CXXFLAGS" \
  --extra-ldflags="$EXTRA_LDFLAGS" \
  --enable-pic

# 自动检测 CPU 核心数
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

echo
echo "✅ FFmpeg successfully built and installed to: $PREFIX"
echo "   All static libs compiled with -fPIC, safe for shared linking."
