#!/bin/zsh
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT

set -euo pipefail

test_dir=${0:A:h}
source "$test_dir/../build-mesa-windows-arm64.sh"

scratch=$(mktemp -d "${TMPDIR:-/tmp}/d12-umd-contract-drift.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
checks=0

check() {
    checks=$((checks + 1))
    if ! eval "$1"; then
        print -u2 "check failed: $1"
        exit 1
    fi
}

write_compile_commands() {
    local output=$1
    local include=$2
    python3 - "$output" "$include" <<'PY'
import json
import sys
from pathlib import Path

Path(sys.argv[1]).write_text(json.dumps([{
    "directory": str(Path(sys.argv[1]).parent),
    "command": f"clang -I{sys.argv[2]} -c d12_umd_adapter.c",
    "file": "../src/asahi/d3d12/umd/d12_umd_adapter.c",
}]), encoding="utf-8")
PY
}

old_contract=$scratch/old-contract
new_contract=$scratch/new-contract
mkdir -p "$old_contract" "$new_contract"
touch "$old_contract/agxw_contract.h" "$new_contract/agxw_contract.h"

# A matching configured contract remains in place, including user-added files.
current=$scratch/current
mkdir -p "$current/meson-private"
touch "$current/build.ninja" "$current/user-artifact"
touch "$current/meson-private/coredata.dat"
write_compile_commands "$current/compile_commands.json" "$new_contract"
d12_umd_refresh_cross_build_contract "$current" "$new_contract"
check '[[ -d $current ]]'
check '[[ -f $current/user-artifact ]]'
check '[[ ! -e $current.contract-path-stale ]]'

# A drifted contract rehomes the whole generated build without deleting files.
stale=$scratch/stale
mkdir -p "$stale/meson-private"
touch "$stale/build.ninja" "$stale/user-artifact"
touch "$stale/meson-private/coredata.dat"
write_compile_commands "$stale/compile_commands.json" "$old_contract"
d12_umd_refresh_cross_build_contract "$stale" "$new_contract"
check '[[ ! -e $stale ]]'
check '[[ -d $stale.contract-path-stale ]]'
check '[[ -f $stale.contract-path-stale/user-artifact ]]'
check '[[ -f $stale.contract-path-stale/build.ninja ]]'

# Existing preserved builds are never overwritten.
collision=$scratch/collision
mkdir -p "$collision/meson-private" "$collision.contract-path-stale"
touch "$collision/build.ninja" "$collision/user-artifact"
touch "$collision/meson-private/coredata.dat"
touch "$collision.contract-path-stale/older-artifact"
write_compile_commands "$collision/compile_commands.json" "$old_contract"
d12_umd_refresh_cross_build_contract "$collision" "$new_contract"
check '[[ -f $collision.contract-path-stale/older-artifact ]]'
check '[[ -f $collision.contract-path-stale.1/user-artifact ]]'
check '[[ ! -e $collision ]]'

# An uninspectable generated build fails safely and is left untouched.
unknown=$scratch/unknown
mkdir -p "$unknown/meson-private"
touch "$unknown/build.ninja" "$unknown/user-artifact"
touch "$unknown/meson-private/coredata.dat"
if d12_umd_refresh_cross_build_contract "$unknown" "$new_contract" \
      2>/dev/null; then
    print -u2 "uninspectable build unexpectedly accepted"
    exit 1
else
    inspect_result=$?
fi
check '(( inspect_result == 2 ))'
check '[[ -f $unknown/user-artifact ]]'
check '[[ ! -e $unknown.contract-path-stale ]]'

# Malformed Meson metadata is not evidence of drift and is also preserved.
malformed=$scratch/malformed
mkdir -p "$malformed/meson-private"
touch "$malformed/build.ninja" "$malformed/user-artifact"
touch "$malformed/meson-private/coredata.dat"
python3 - "$malformed/compile_commands.json" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[1]).write_text('{}', encoding='utf-8')
PY
if d12_umd_refresh_cross_build_contract "$malformed" "$new_contract" \
      2>/dev/null; then
    print -u2 "malformed build metadata unexpectedly accepted"
    exit 1
else
    inspect_result=$?
fi
check '(( inspect_result == 2 ))'
check '[[ -f $malformed/user-artifact ]]'
check '[[ ! -e $malformed.contract-path-stale ]]'

# A directory that is not a complete Meson build is never moved.
unrecognized=$scratch/unrecognized
mkdir -p "$unrecognized"
touch "$unrecognized/build.ninja" "$unrecognized/user-artifact"
write_compile_commands "$unrecognized/compile_commands.json" "$old_contract"
if d12_umd_refresh_cross_build_contract "$unrecognized" "$new_contract" \
      2>/dev/null; then
    print -u2 "unrecognized build directory unexpectedly accepted"
    exit 1
else
    inspect_result=$?
fi
check '(( inspect_result == 2 ))'
check '[[ -f $unrecognized/user-artifact ]]'
check '[[ ! -e $unrecognized.contract-path-stale ]]'

# Even Meson-shaped metadata cannot make the source root a movable target.
source_like=$scratch/source-like
mkdir -p "$source_like/meson-private"
touch "$source_like/build.ninja" "$source_like/user-artifact"
touch "$source_like/meson-private/coredata.dat"
write_compile_commands "$source_like/compile_commands.json" "$old_contract"
if d12_umd_refresh_cross_build_contract "$source_like" "$new_contract" \
      "$source_like" 2>/dev/null; then
    print -u2 "source directory unexpectedly accepted"
    exit 1
else
    inspect_result=$?
fi
check '(( inspect_result == 2 ))'
check '[[ -f $source_like/user-artifact ]]'
check '[[ ! -e $source_like.contract-path-stale ]]'

# A not-yet-configured destination is a no-op ready for normal Meson setup.
fresh=$scratch/fresh
d12_umd_refresh_cross_build_contract "$fresh" "$new_contract"
check '[[ ! -e $fresh ]]'

print "test_build_mesa_windows_arm64: PASS ($checks checks)"
