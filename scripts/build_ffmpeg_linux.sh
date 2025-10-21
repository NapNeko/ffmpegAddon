#!/usr/bin/env bash
set -euo pipefail

# build_ffmpeg_unix.sh
# Usage: build_ffmpeg_unix.sh <prefix>
# Example: build_ffmpeg_unix.sh /home/runner/work/repo/buildout

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
  --enable-static \
  --disable-shared \
  --disable-programs \
  --disable-doc \
  --disable-debug \
  --disable-ffplay \
  --disable-ffprobe \
  --enable-small \
  --enable-avcodec \
  --enable-avformat \
  --enable-swresample \
  --enable-swscale \
  --enable-avutil \
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
