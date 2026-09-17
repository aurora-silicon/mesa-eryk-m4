#!/bin/bash
# Build the reference screenshot overlay with the shared ARM64 desktop sysroot.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
graphics=${M4_GRAPHICS_WORKSPACE:-/home/council/omarchy/graphics-build}
export PKG_CONFIG_SYSROOT_DIR="$graphics/sysroot"
export PKG_CONFIG_LIBDIR="$graphics/sysroot/usr/lib/pkgconfig:$graphics/sysroot/usr/share/pkgconfig"
export PATH="$graphics/host-tools:$PATH"
cmake -S "$root/reference/hyprpicker-0.4.7" -B "$root/build/hyprpicker-m4-arm64" -G Ninja \
  --toolchain "$graphics/aarch64-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/build/hyprpicker-m4-arm64" -j "${1:-8}"
