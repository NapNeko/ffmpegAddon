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

# Run configure with a conservative set of options similar to previous workflow
# Build static libs but ensure objects are compiled with -fPIC so they can be linked
# into shared objects (Node addon). We set EXTRA_CFLAGS and EXTRA_LDFLAGS to
# request position-independent code. If you prefer shared libraries, change
# --enable-static/--disable-shared accordingly.
EXTRA_CFLAGS="-fPIC"
EXTRA_LDFLAGS=""

./configure --prefix="$PREFIX" \
  --enable-static --disable-shared --disable-programs --disable-doc --disable-debug \
  --disable-ffplay --disable-ffprobe --enable-small --enable-avcodec --enable-avformat \
  --enable-swresample --enable-swscale --enable-avutil \
  "--extra-cflags=$EXTRA_CFLAGS" "--extra-ldflags=$EXTRA_LDFLAGS"

# Determine CPU count safely for Linux and macOS
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
