#!/bin/bash
# Native host GL/Vulkan build for desktop and FEX graphics thunks.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
meson setup "$root/build/mesa-m4-native-release" "$root/mesa/m4-asahi" \
  --cross-file "$root/build/mesa-m4-aarch64.ini" \
  --native-file "$root/build/mesa-asahi-native-tools.ini" \
  --buildtype=release -Ddebug=true -Dgallium-drivers=zink -Dvulkan-drivers=asahi \
  -Dplatforms=x11,wayland -Dllvm=disabled -Dglx=dri \
  -Degl=enabled -Dgbm=enabled -Dtools=drm-shim \
  -Dmesa-clc=system -Dprecomp-compiler=system
ninja -C "$root/build/mesa-m4-native-release" -j "${1:-12}" \
  src/asahi/vulkan/libvulkan_asahi.so \
  src/gallium/targets/dri/libgallium-26.1.4.so
