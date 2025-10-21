#!/usr/bin/env bash
set -euo pipefail

# FFmpeg minimal build script for MSVC (via MSYS2)
# Works when run inside MSYS2 shell started from
# "x64 Native Tools Command Prompt for VS 2022"
# Usage:
#   ./scripts/build_ffmpeg_msvc_trim.sh [install_prefix]

# default install prefix
PREFIX="$(pwd)/buildout"
if [ "$#" -ge 1 ]; then
  PREFIX="$1"
fi

# Convert MSYS path (/e/... style) to Windows style for MSVC
if [[ "$PREFIX" == /* ]]; then
  PREFIX_WIN=$(cygpath -m "$PREFIX")
else
  PREFIX_WIN="$PREFIX"
fi

echo "Install prefix (MSYS): $PREFIX"
echo "Install prefix (Windows): $PREFIX_WIN"
echo

CONFIGURE_FLAGS=(
  --prefix="$PREFIX_WIN"
  --toolchain=msvc
  --arch=x86_64
  --target-os=win64
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

echo "Configuring with minimal MSVC options..."
cd ffmpeg_src
./configure "${CONFIGURE_FLAGS[@]}"

echo "Running make -j$(nproc || echo 4)"
make -j"$(nproc || echo 4)"
echo "Running make install"
make install

echo
echo "✅ Build complete. Installed to: $PREFIX_WIN"
cd ..
cat <<'EOF'
Notes:
- This build is minimal: supports audio decode to PCM and container demuxing.
- Ensure you're running this from an MSYS2 shell launched via Visual Studio Developer Command Prompt.
- If you see 'cl.exe is unable to create an executable file', check that vcvars64.bat has been run.
EOF
