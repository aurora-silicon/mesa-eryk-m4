#!/bin/sh
set -eu

package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
frames=${1:-600}

export DISPLAY=${DISPLAY:-:0}
export VK_DRIVER_FILES="$package_dir/g17p_wsi_triangle_icd.json"
export LD_LIBRARY_PATH="$package_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

exec "$package_dir/g17p-wsi-triangle" "$frames"
