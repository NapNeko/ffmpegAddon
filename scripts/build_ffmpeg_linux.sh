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

./configure --prefix="$PREFIX" \
  --enable-static --disable-shared --disable-programs --disable-doc --disable-debug \
  --disable-ffplay --disable-ffprobe --enable-small --enable-avcodec --enable-avformat \
  --enable-swresample --enable-swscale --enable-avutil

if command -v nproc >/dev/null 2>&1; then
  JOBS=$(nproc)
elif [[ "$(uname)" == "Darwin" ]]; then
  JOBS=$(sysctl -n hw.ncpu)
else
  JOBS=1
fi

echo "Running make -j$JOBS"
make -j${JOBS}
make install

echo "FFmpeg built and installed to: $PREFIX"
