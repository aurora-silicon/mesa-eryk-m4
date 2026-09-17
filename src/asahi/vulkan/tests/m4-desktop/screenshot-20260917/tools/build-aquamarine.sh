#!/bin/bash
# Build the maintained ARM64 display backend against the Omarchy sysroot.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
graphics=${M4_GRAPHICS_WORKSPACE:-/home/council/omarchy/graphics-build}
export PKG_CONFIG_SYSROOT_DIR="$graphics/sysroot"
export PKG_CONFIG_LIBDIR="$graphics/sysroot/usr/lib/pkgconfig:$graphics/sysroot/usr/share/pkgconfig"
export PATH="$graphics/host-tools:$PATH"
cmake -S "$root/reference/aquamarine-m4" -B "$root/build/aquamarine-m4-arm64" -G Ninja \
  --toolchain "$graphics/aarch64-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/build/aquamarine-m4-arm64" -j "${1:-8}"
