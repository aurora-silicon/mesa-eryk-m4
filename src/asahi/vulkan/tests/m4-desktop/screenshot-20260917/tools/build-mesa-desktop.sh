#!/bin/bash
# Build a release desktop driver separately from the assertion-enabled CTS build.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
meson setup "$root/build/mesa-m4-desktop-release" "$root/mesa/m4-asahi" \
  --cross-file "$root/build/mesa-m4-aarch64.ini" \
  --native-file "$root/build/mesa-asahi-native-tools.ini" \
  --buildtype=release -Dgallium-drivers=zink -Dvulkan-drivers=asahi \
  -Dplatforms=x11,wayland -Dllvm=disabled -Dglx=disabled \
  -Degl=enabled -Dgbm=enabled -Dtools=drm-shim \
  -Dmesa-clc=system -Dprecomp-compiler=system
ninja -C "$root/build/mesa-m4-desktop-release" -j "${1:-12}" \
  src/asahi/vulkan/libvulkan_asahi.so \
  src/gallium/targets/dri/libgallium-26.1.4.so
