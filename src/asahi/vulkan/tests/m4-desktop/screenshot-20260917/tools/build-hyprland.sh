#!/bin/bash
# Incremental ARM64 compositor build using the existing Omarchy sysroot.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
graphics=${M4_GRAPHICS_WORKSPACE:-/home/council/omarchy/graphics-build}
export PKG_CONFIG_SYSROOT_DIR="$graphics/sysroot"
export PKG_CONFIG_LIBDIR="$graphics/sysroot/usr/lib/pkgconfig:$graphics/sysroot/usr/share/pkgconfig"
export PATH="$graphics/host-tools:$PATH"
git -C "$root/reference/hyprland-0.56.1" submodule update --init subprojects/hyprland-protocols subprojects/udis86
cmake -S "$root/reference/hyprland-0.56.1" -B "$root/build/hyprland-m4-arm64" -G Ninja \
    --toolchain "$graphics/aarch64-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
    -DNO_HYPRPM=ON -DNO_UWSM=ON -DPython3_EXECUTABLE=/usr/bin/python3.14
cmake --build "$root/build/hyprland-m4-arm64" --target Hyprland -j "${1:-8}"
