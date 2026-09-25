#!/bin/sh
# SPDX-License-Identifier: MIT
# Rebuild the G15G stall corpus with Apple's toolchain and check the rule.
# Offline compilation only; no GPU is used. Needs Xcode's Metal toolchain.
#
#   sh run-corpus.sh [WORK]          (default /tmp/g15g-stalls-work)
#
# Optional: AURORA_MESA=<aurora mesa clone> adds its agx3-integer-uniform and
# agx3-optimization corpora (read with git show origin/agx3:..., not checked
# out).
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler=$(CDPATH= cd -- "$here/../../.." && pwd)
WORK=${1:-${G15G_WORK:-/tmp/g15g-stalls-work}}
export G15G_WORK="$WORK"
export DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
mkdir -p "$WORK"
cd "$WORK"

cc=${CC:-cc}
C="$compiler/agx3_compose"
$cc -O2 -I"$C" "$here/decomp_batch.c" "$C/agx3_compose.c" "$C/agx3b_tables.c" "$C/agx3c_tables.c" -o decomp
$cc -O2 -I"$C" "$here/fieldbits.c" "$C/agx3_compose.c" "$C/agx3b_tables.c" "$C/agx3c_tables.c" -o fieldbits
$cc -O2 -I"$compiler" -I"$C" -I"$compiler/test" "$compiler/test/test-agx3-g15g-stalls.c" \
    "$compiler/agx3_g15g_stalls.c" "$C/agx3_decompose.c" "$C/agx3_compose.c" \
    "$C/agx3b_tables.c" "$C/agx3c_tables.c" -o test-g15g-stalls

xcrun metal --version | head -1 > toolchain.txt
"$(dirname "$(xcrun -f metal)")/metal-nt" --version | head -1 >> toolchain.txt
sw_vers >> toolchain.txt

py=python3
$py "$here/gen_corpus.py" "$WORK"
grep -oE 'kernel void [a-z0-9_]+' "$here/targeted.metal" | awk '{print $3}' > targeted.txt
grep -oE 'fragment [a-z0-9]+ [a-z0-9_]+' "$here/render.metal" | awk '{print $3}' > render.txt
$py "$here/fuzz.py" 7 200 && mv fuzz.metal fuzz1.metal && mv fuzz.txt fuzz1.txt
$py "$here/fuzz.py" 99 100
$py "$here/fuzz2.py" 2026 300 fuzz3
$py "$here/fuzz2.py" 4242 150 fuzz4

$py "$here/compile.py" kernels.metal cases.txt main.json
$py "$here/compile.py" --fast-math kernels.metal cases.txt main-fast.json
$py "$here/compile.py" "$here/targeted.metal" targeted.txt targeted.json
$py "$here/compile.py" --fast-math "$here/targeted.metal" targeted.txt targeted-fast.json
$py "$here/compile.py" --render "$here/render.metal" render.txt render.json
$py "$here/compile.py" fuzz1.metal fuzz1.txt fuzz.json
$py "$here/compile.py" --fast-math fuzz.metal fuzz.txt fuzz2.json
$py "$here/compile.py" fuzz3.metal fuzz3.txt fuzz3.json
$py "$here/compile.py" --fast-math fuzz4.metal fuzz4.txt fuzz4.json
sets="main main-fast targeted targeted-fast render fuzz fuzz2 fuzz3 fuzz4"

if [ -n "${AURORA_MESA:-}" ]; then
    for c in agx3-integer-uniform-corpus:intuni agx3-optimization-corpus:opt; do
        dir=${c%%:*}; tag=${c##*:}
        git -C "$AURORA_MESA" show "origin/agx3:src/asahi/compiler/test/probes/$dir/kernels.metal" > "$tag.metal"
        git -C "$AURORA_MESA" show "origin/agx3:src/asahi/compiler/test/probes/$dir/cases.txt" > "$tag.txt"
        $py "$here/compile.py" "$tag.metal" "$tag.txt" "$tag.json"
        sets="$sets $tag"
    done
fi

jsons=$(for s in $sets; do printf '%s.json ' "$s"; done)
echo "== reference model (predict.py)"
$py "$here/predict.py" $jsons | head -20
echo "== G15G minus stalls vs G15S (claim.py)"
$py "$here/claim.py" $jsons
$py "$here/mkvectors.py" $jsons > vectors.txt
echo "== C pass (agx3_g15g_stalls.c) replaying every shader"
./test-g15g-stalls vectors.txt | tail -1
