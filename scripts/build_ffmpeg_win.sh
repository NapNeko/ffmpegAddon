#!/usr/bin/env bash
set -euo pipefail

PREFIX=${1:-}
if [ -z "$PREFIX" ]; then
  echo "Usage: $0 <install-prefix>" >&2
  exit 2
fi

echo "Install prefix: $PREFIX"

# Create pkg-config file for lame if it doesn't exist
mkdir -p "$PREFIX/lib/pkgconfig"
if [ ! -f "$PREFIX/lib/pkgconfig/lame.pc" ]; then
  cat > "$PREFIX/lib/pkgconfig/lame.pc" << EOF
prefix=$PREFIX
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: lame
Description: LAME MP3 encoder
Version: 3.100
Libs: -L\${libdir} -lmp3lame
Cflags: -I\${includedir}
EOF
  echo "Created pkg-config file for lame"
fi

# Set PKG_CONFIG_PATH to include our prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# Debug: Check if pkg-config can find lame
echo "Checking pkg-config for lame..."
pkg-config --exists lame && echo "pkg-config found lame" || echo "WARNING: pkg-config cannot find lame"
pkg-config --modversion lame 2>/dev/null || echo "Could not get lame version"
pkg-config --cflags lame 2>/dev/null || echo "Could not get lame cflags"
pkg-config --libs lame 2>/dev/null || echo "Could not get lame libs"

CONFIGURE_FLAGS=(
  --prefix="$PREFIX" \
  --extra-cflags="-I$PREFIX/include" \
  --extra-ldflags="-L$PREFIX/lib" \
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
