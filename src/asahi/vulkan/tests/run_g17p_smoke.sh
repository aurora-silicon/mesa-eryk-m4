#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
render_node=${1:-/dev/dri/renderD128}
frames=${2:-60}

case "$frames" in
   ''|*[!0-9]*|0)
      echo "usage: $0 [render-node] [positive-frame-count]" >&2
      exit 2
      ;;
esac

for file in \
   asahi-vulkan-g17p-enumerate \
   asahi-submit-g17p-add3 \
   g17p-wsi-triangle
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

if [ ! -c "$render_node" ]; then
   echo "render node is not a character device: $render_node" >&2
   exit 2
fi

stage_path=/sys/module/asahi/parameters/g17p_test_stage
if [ -r "$stage_path" ]; then
   IFS= read -r stage < "$stage_path"
   if [ "$stage" != 2 ]; then
      echo "asahi is at g17p_test_stage=$stage; reload it with g17p_test_stage=2" >&2
      exit 2
   fi
fi

export DISPLAY=${DISPLAY:-:0}
export VK_DRIVER_FILES="$package_dir/g17p_wsi_triangle_icd.json"
export LD_LIBRARY_PATH="$package_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MESA_SHADER_CACHE_DISABLE=true

echo "[Neo Vulkan] 1/3 Enumerating T8140/G17P"
"$package_dir/asahi-vulkan-g17p-enumerate"

echo "[Neo Vulkan] 2/3 Submitting 64-float add3 on $render_node"
"$package_dir/asahi-submit-g17p-add3" "$render_node"

echo "[Neo Vulkan] 3/3 Presenting $frames triangle frames on $DISPLAY"
"$package_dir/g17p-wsi-triangle" "$frames"

echo "[Neo Vulkan] All three checks passed"
