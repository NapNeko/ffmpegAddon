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
  --enable-static
  --disable-programs
  --disable-shared
  --disable-ffplay
  --disable-ffprobe
  --disable-doc
  --disable-debug
  --enable-stripping
  --enable-small
  
  --enable-avcodec
  --enable-avformat
  --enable-swscale
  --enable-swresample
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
