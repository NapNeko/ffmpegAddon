#!/usr/bin/env bash
set -euo pipefail

PREFIX=${1:-}
if [ -z "$PREFIX" ]; then
  echo "Usage: $0 <install-prefix>" >&2
  exit 2
fi

echo "Install prefix: $PREFIX"

# Create pkg-config file for lame with MSVC-compatible flags
mkdir -p "$PREFIX/lib/pkgconfig"

# Convert to Windows paths for pkg-config
PREFIX_WIN_FOR_PC=$(cygpath -w "$PREFIX" 2>/dev/null || echo "$PREFIX")
# Escape backslashes for pkg-config file
PREFIX_WIN_FOR_PC=$(echo "$PREFIX_WIN_FOR_PC" | sed 's/\\/\\\\/g')

cat > "$PREFIX/lib/pkgconfig/lame.pc" << EOF
prefix=$PREFIX_WIN_FOR_PC
exec_prefix=\${prefix}
libdir=\${exec_prefix}\\lib
includedir=\${prefix}\\include

Name: lame
Description: LAME MP3 encoder
Version: 3.100
Libs: -LIBPATH:\${libdir} mp3lame.lib
Cflags: -I\${includedir}
EOF
echo "Created pkg-config file for lame with MSVC-style flags"

# Set PKG_CONFIG_PATH to include our prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# Debug: Check if pkg-config can find lame
echo "Checking pkg-config for lame..."
pkg-config --exists lame && echo "pkg-config found lame" || echo "WARNING: pkg-config cannot find lame"
pkg-config --modversion lame 2>/dev/null || echo "Could not get lame version"
pkg-config --cflags lame 2>/dev/null || echo "Could not get lame cflags"
pkg-config --libs lame 2>/dev/null || echo "Could not get lame libs"

# Verify LAME library file exists
echo "Checking for LAME library file..."
if [ -f "$PREFIX/lib/mp3lame.lib" ]; then
  echo "Found: $PREFIX/lib/mp3lame.lib"
else
  echo "ERROR: mp3lame.lib not found in $PREFIX/lib"
  ls -la "$PREFIX/lib" 2>/dev/null || echo "Could not list $PREFIX/lib"
  exit 1
fi

# Convert Windows path to proper format for MSVC
PREFIX_WIN=$(cygpath -w "$PREFIX" 2>/dev/null || echo "$PREFIX")
PREFIX_WIN_LIB=$(cygpath -w "$PREFIX/lib" 2>/dev/null || echo "$PREFIX/lib")
PREFIX_WIN_INC=$(cygpath -w "$PREFIX/include" 2>/dev/null || echo "$PREFIX/include")

# Set environment variables for MSVC linker
export LIB="$PREFIX_WIN_LIB;$LIB"
export INCLUDE="$PREFIX_WIN_INC;$INCLUDE"

CONFIGURE_FLAGS=(
  --prefix="$PREFIX" \
  --extra-cflags="-I$PREFIX_WIN_INC" \
  --extra-ldflags="-LIBPATH:$PREFIX_WIN_LIB" \
  --extra-libs="mp3lame.lib" \
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


echo "Configuring FFmpeg with MSVC toolchain..."
if ! ./configure "${CONFIGURE_FLAGS[@]}"; then
  echo "Configure failed! Displaying config.log:"
  cat ffbuild/config.log || echo "Could not read config.log"
  exit 1
fi

echo "Running make -j$(nproc || echo 4)"
make -j"$(nproc || echo 4)"

echo "Running make install"
make install

mkdir -p "$PREFIX"
cp -r "$(pwd)/install/"* "$PREFIX" || true

cd ..
