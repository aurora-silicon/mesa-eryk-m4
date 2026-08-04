#!/bin/bash
# Run every d3d12agx GPU acceptance suite with its correct shader arguments.
#
# The argument lists are the part that is easy to get wrong -- each test takes
# a different set of shaders, and a wrong name makes a test print usage and
# exit 0, which reads as a pass in a loop that only checks the last line.
# Keep this file as the single source of truth. A suite that produces no
# verdict line at all is a FAILURE here, not a silent pass.
#
# Usage: gpusuite.sh [path-to-mesa-build]
set -u

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
S=$HERE/shaders
B=${1:-$HERE/../../../../_build/src/asahi/d3d12}

run() {
   local name=$1; shift
   local bin=$B/d3d12agx_$name
   [ -x "$bin" ] || { printf "%-18s MISSING BINARY\n" "$name"; return 1; }
   for f in "$@"; do
      [ -r "$f" ] || { printf "%-18s MISSING SHADER %s\n" "$name" "$(basename "$f")"; return 1; }
   done
   local out rc last
   out=$(timeout 180 "$bin" "$@" 2>&1); rc=$?
   last=$(echo "$out" | grep -E "ALL PASS|FAILURES" | tail -1)
   if [ -z "$last" ]; then
      printf "%-18s NO VERDICT (exit %d)\n" "$name" "$rc"
      echo "$out" | tail -3 | sed 's/^/                   | /'
      return 1
   fi
   printf "%-18s %s\n" "$name" "$last"
   echo "$out" | grep "^FAIL" | sed 's/^/                   | /'
   [ $rc -eq 0 ]
}

fails=0
run g4_triangle     "$S/tri.dxil" "$S/solid.dxil"                                  || fails=$((fails+1))
run g5_depth        "$S/tri.dxil" "$S/solid.dxil" "$S/tri_depth.dxil" \
                    "$S/solid_g.dxil" "$S/tri_depth_cw.dxil"                       || fails=$((fails+1))
run g5_descriptors  "$S/res_probe.dxil" "$S/rs_table.rs" "$S/rs_mixed.rs" \
                    "$S/rs_descriptors.rs" "$S/space_probe.dxil"                   || fails=$((fails+1))
run g5_atomics      "$S/atomic_probe.dxil"                                         || fails=$((fails+1))
run g5_control_flow "$S/cf_probe.dxil"                                             || fails=$((fails+1))
run g5_wave         "$S/wave_probe.dxil"                                           || fails=$((fails+1))
[ -x "$B/d3d12agx_g5_texture" ] && { run g5_texture "$S/tex_probe.dxil" || fails=$((fails+1)); }

# G6 needs a geometry shader, and the only ones available are the corpus's
# -- LGPL-derived from vkd3d-proton, so not vendored here. Point
# D3D12AGX_CORPUS at a directory holding them to run it. Skipped, loudly,
# rather than silently absent: a suite that vanishes reads as a suite that
# passed.
GS_VS="$D3D12AGX_CORPUS/vrs_vs_code_dxil.dxil"
GS_GS="$D3D12AGX_CORPUS/gs_topology_point_code_dxil.dxil"
if [ -n "$D3D12AGX_CORPUS" ] && [ -f "$GS_VS" ] && [ -f "$GS_GS" ]; then
   run g6_geometry "$GS_VS" "$S/solid.dxil" "$GS_GS" || fails=$((fails+1))
else
   printf "%-18s %s\n" "g6_geometry" \
      "SKIPPED - set D3D12AGX_CORPUS to a directory with the corpus shaders"
fi

echo
if [ $fails -eq 0 ]; then echo "GPU SUITES: all green"; else echo "GPU SUITES: $fails suite(s) not green"; fi
exit $fails
