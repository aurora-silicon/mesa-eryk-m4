#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

package_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)

for file in \
   asahi-vulkan-g17p-enumerate \
   g17p-vulkan-compute \
   g17p-offscreen-triangle
do
   if [ ! -x "$package_dir/$file" ]; then
      echo "missing package executable: $package_dir/$file" >&2
      exit 2
   fi
done

for file in \
   libvulkan_asahi.so \
   g17p_wsi_triangle_icd.json
do
   if [ ! -r "$package_dir/$file" ]; then
      echo "missing package file: $package_dir/$file" >&2
      exit 2
   fi
done

export VK_DRIVER_FILES="$package_dir/g17p_wsi_triangle_icd.json"
export LD_LIBRARY_PATH="$package_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MESA_SHADER_CACHE_DISABLE=true

echo "[Neo Vulkan] 1/3 Enumerating T8140/G17P"
"$package_dir/asahi-vulkan-g17p-enumerate"

echo "[Neo Vulkan] 2/3 Dispatching one private buffer-only compute group"
"$package_dir/g17p-vulkan-compute"

echo "[Neo Vulkan] 3/3 Rendering and reading back one offscreen triangle"
"$package_dir/g17p-offscreen-triangle"

echo "[Neo Vulkan] Enumeration, compute, and offscreen triangle passed"
