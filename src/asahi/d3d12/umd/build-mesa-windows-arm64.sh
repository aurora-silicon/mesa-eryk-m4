#!/bin/zsh
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT
#
# Reproducibly cross-build both native AGX D3D12 frontends against the one
# renderer-core archive: the public COM DLL and the WDDM UMD DDI DLL.

# Meson snapshots cross-file properties in coredata.dat.  Reconfiguring after
# regenerating the cross-file therefore does not guarantee that a relocated
# external contract include is adopted.  Keep these helpers sourceable so the
# preservation behavior can be tested without requiring a WDK/toolchain.
d12_umd_build_uses_contract() {
    local build_dir=$1
    local contract_dir=$2
    local compile_commands=$build_dir/compile_commands.json

    [[ -f $compile_commands ]] || return 2
    python3 - "$compile_commands" "$contract_dir" <<'PY'
import json
import shlex
import sys
from pathlib import Path

commands_path = Path(sys.argv[1])
expected = Path(sys.argv[2]).resolve()

try:
    commands = json.loads(commands_path.read_text(encoding="utf-8"))
except (OSError, ValueError) as error:
    print(f"cannot inspect Meson compile database {commands_path}: {error}",
          file=sys.stderr)
    raise SystemExit(2)

if not isinstance(commands, list):
    print(f"invalid compile database root in {commands_path}", file=sys.stderr)
    raise SystemExit(2)

d12_command_seen = False
include_seen = False
for entry in commands:
    if not isinstance(entry, dict):
        print(f"invalid compile command entry in {commands_path}",
              file=sys.stderr)
        raise SystemExit(2)

    source = entry.get("file")
    if not isinstance(source, str):
        print(f"compile command without a source in {commands_path}",
              file=sys.stderr)
        raise SystemExit(2)
    if "src/asahi/d3d12/" not in source.replace("\\", "/"):
        continue
    d12_command_seen = True

    arguments = entry.get("arguments")
    if arguments is None:
        try:
            arguments = shlex.split(entry["command"])
        except (KeyError, TypeError, ValueError) as error:
            print(f"invalid compile command in {commands_path}: {error}",
                  file=sys.stderr)
            raise SystemExit(2)
    if not isinstance(arguments, list) or not all(
            isinstance(argument, str) for argument in arguments):
        print(f"invalid argument list in {commands_path}", file=sys.stderr)
        raise SystemExit(2)

    index = 0
    while index < len(arguments):
        argument = arguments[index]
        include = None
        if argument in ("-I", "-isystem"):
            index += 1
            if index >= len(arguments):
                print(f"incomplete include argument in {commands_path}",
                      file=sys.stderr)
                raise SystemExit(2)
            include = arguments[index]
        elif argument.startswith("-I") and len(argument) > 2:
            include = argument[2:]
        elif argument.startswith("-isystem") and len(argument) > 8:
            include = argument[8:]

        if include is not None:
            if not include:
                print(f"empty include argument in {commands_path}",
                      file=sys.stderr)
                raise SystemExit(2)
            include_seen = True
            if Path(include).resolve() == expected:
                raise SystemExit(0)
        index += 1

if not d12_command_seen or not include_seen:
    print(f"no inspectable D3D12 compile command in {commands_path}",
          file=sys.stderr)
    raise SystemExit(2)

raise SystemExit(1)
PY
}

d12_umd_refresh_cross_build_contract() {
    local build_dir=${1%/}
    local contract_dir=$2
    local source_dir=${3:-}
    local inspect_status
    local preserved
    local ordinal=1

    [[ -n $build_dir && $build_dir != / ]] || {
        print -u2 "refusing unsafe D3D12 UMD build directory: $1"
        return 2
    }
    if [[ -n $source_dir && ${build_dir:A} == ${source_dir:A} ]]; then
        print -u2 "refusing to rehome the D3D12 UMD source directory: $build_dir"
        return 2
    fi
    [[ -f $build_dir/build.ninja ]] || return 0
    [[ -f $build_dir/meson-private/coredata.dat ]] || {
        print -u2 "refusing to rehome an unrecognized build directory: $build_dir"
        return 2
    }

    if d12_umd_build_uses_contract "$build_dir" "$contract_dir"; then
        return 0
    else
        inspect_status=$?
    fi
    if (( inspect_status != 1 )); then
        print -u2 "cannot verify the configured AGX WDDM contract in $build_dir"
        return $inspect_status
    fi

    preserved=$build_dir.contract-path-stale
    while [[ -e $preserved || -L $preserved ]]; do
        preserved=$build_dir.contract-path-stale.$ordinal
        ordinal=$((ordinal + 1))
    done

    print -u2 "AGX WDDM contract path changed; preserving the stale Meson build at:"
    print -u2 "  $preserved"
    mv "$build_dir" "$preserved"
}

if [[ ${ZSH_EVAL_CONTEXT:-} == *:file ]]; then
    return 0
fi

set -euo pipefail

umd_dir=${0:A:h}
d12_dir=${umd_dir:h}
repo_root=${umd_dir:h:h:h:h}
workspace_root=${repo_root:h}
asahi_workspace=${AURORA_ASAHI_WORKSPACE:-/Users/dj/Developer/Asahi/windows-on-apple-silicon}
main_contract=$workspace_root/AuroraSilicon/drivers/AppleAgxWddm/core
contract_compatible() {
    [[ -f $1/agxw_contract.h ]] &&
        grep -Eq '^#define[[:space:]]+AGXW_CONTRACT_VERSION[[:space:]]+([3-9]|[1-9][0-9]+)u' "$1/agxw_contract.h" &&
        grep -q 'submit_private_max_version' "$1/agxw_contract.h" &&
        grep -q 'AGXW_SUBMIT_PRIVATE_VERSION_V4' "$1/agxw_contract.h" &&
        grep -q 'AGXW_SUBMIT_PRIVATE_VERSION_V5' "$1/agxw_contract.h" &&
        grep -q 'AGXW_SUBMIT_CLEAN_SET_COMPLETE' "$1/agxw_contract.h"
}
if [[ -n ${AGX_WDDM_CONTRACT_CORE:-} ]]; then
    contract_dir=$AGX_WDDM_CONTRACT_CORE
elif contract_compatible "$main_contract"; then
    contract_dir=$main_contract
else
    contract_dir=$main_contract
fi
if ! contract_compatible "$contract_dir"; then
    print -u2 "latest merged AGX WDDM contract (v4 placement + v5 clean sets) is required: $contract_dir"
    print -u2 "set AGX_WDDM_CONTRACT_CORE to the latest AppleAgxWddm core directory"
    exit 2
fi
wdk_root=${WDK_ROOT:-$asahi_workspace/build/deps/wdk-arm64-26100.6584/package/c}
sdk_root=${SDK_ROOT:-$asahi_workspace/build/deps/windows-sdk-cpp-26100.6584/package/c}
wdk_include=$wdk_root/Include/10.0.26100.0
sdk_include=$sdk_root/Include/10.0.26100.0
toolchain=${LLVM_MINGW_ROOT:-/Users/dj/Toolchains/llvm-mingw}
readobj=${LLVM_READOBJ:-/opt/homebrew/opt/llvm/bin/llvm-readobj}
native_build=${D12_UMD_NATIVE_BUILD:-$repo_root/_build-d3d12-umd-native-tools}
cross_build=${D12_UMD_MESA_BUILD:-$repo_root/_build-d3d12-umd-mesa-arm64-shared}
wddm_target=${D12_UMD_WDDM_TARGET:-21}
case $wddm_target in
    20) wddm_version=2.0; driver_version=20.17.240.0 ;;
    21) wddm_version=2.1; driver_version=21.17.240.0 ;;
    23) wddm_version=2.3; driver_version=23.17.240.0 ;;
    24) wddm_version=2.4; driver_version=24.17.240.0 ;;
    31) wddm_version=3.1; driver_version=31.17.240.0 ;;
    *) print -u2 "D12_UMD_WDDM_TARGET must be 20, 21, 23, 24, or 31"; exit 2 ;;
esac
# Meson intentionally rejects absolute include paths that point back inside
# its source tree.  Keep staged WDK/generated headers in the parent workspace
# so they are both disposable and unambiguously external inputs.  Include the
# worktree, build and target identities because staticizing a staged WDK header
# in place is not safe to share between concurrent cross-builds.
support_default=$workspace_root/_build-d3d12-umd-support-${repo_root:t}-${cross_build:t}-${wddm_target}
support_dir=${D12_UMD_SUPPORT_DIR:-$support_default}
python_deps=${D12_UMD_PYTHON_DEPS:-$repo_root/_build-d3d12-umd-python-deps}
staged_wdk=$support_dir/winddk
generated=$support_dir/generated
cross_file=$support_dir/windows-arm64-cross.ini

for required in meson ninja python3 "$toolchain/bin/aarch64-w64-mingw32-clang" \
    "$readobj" "$wdk_include/um/d3d12umddi.h" \
    "$sdk_include/shared/d3dkmthk.h" "$contract_dir/agxw_contract.h"; do
    if [[ $required == */* ]]; then
        [[ -e $required ]] || { print -u2 "missing dependency: $required"; exit 2; }
    else
        command -v "$required" >/dev/null || \
            { print -u2 "missing dependency on PATH: $required"; exit 2; }
    fi
done

if [[ -d $python_deps ]]; then
    export PYTHONPATH=$python_deps${PYTHONPATH:+:$PYTHONPATH}
fi
python3 -c 'import mako, packaging, yaml' >/dev/null || {
    print -u2 "Mesa generator modules missing: install Mako, packaging and PyYAML"
    print -u2 "or point D12_UMD_PYTHON_DEPS at a directory containing them"
    exit 2
}

mkdir -p "$staged_wdk" "$generated"
for header in d3d12umddi.h d3d10umddi.h d3dumddi.h \
    d3d10TokenizedProgramFormat.hpp d3d11TokenizedProgramFormat.hpp \
    d3d12TokenizedProgramFormat.hpp; do
    cp "$wdk_include/um/$header" "$staged_wdk/"
done
cp "$wdk_include/shared/d3dkmddi.h" "$staged_wdk/"
for header in d3dkmthk.h d3dkmdt.h d3dukmdt.h; do
    cp "$sdk_include/shared/$header" "$staged_wdk/"
done
for header in dxgiddi.h dxmini.h; do
    cp "$sdk_include/um/$header" "$staged_wdk/"
done
python3 "$umd_dir/tools/staticize-wdk-c-constants.py" \
    "$staged_wdk/d3d10umddi.h"
python3 "$umd_dir/tools/staticize-wdk-c-constants.py" \
    "$staged_wdk/d3d12umddi.h"
python3 "$umd_dir/tools/generate-ddi-tables.py" \
    --header "$staged_wdk/d3d12umddi.h" \
    --output "$generated/d12_umd_tables_generated.inc"
python3 "$umd_dir/tests/check_ddi_table_coverage.py" \
    --header "$staged_wdk/d3d12umddi.h" \
    --source-dir "$umd_dir"
python3 "$umd_dir/tools/generate-windows-cross-file.py" \
    --template "$umd_dir/windows-arm64-cross.ini.in" \
    --output "$cross_file" \
    --toolchain "$toolchain" \
    --compat "$umd_dir/compat" \
    --contract "$contract_dir" \
    --wdk "$staged_wdk" \
    --generated "$generated"

# Cross-file properties are immutable enough in Meson's persisted core data
# that --reconfigure may retain an old external-property include.  Preserve a
# stale generated build under a sibling name, then let the normal setup path
# create a fresh build at the caller-requested location.
d12_umd_refresh_cross_build_contract "$cross_build" "$contract_dir" \
    "$repo_root"

native_mesa_clc=$native_build/src/compiler/clc/mesa_clc
native_vtn_bindgen=$native_build/src/compiler/spirv/vtn_bindgen2
native_asahi_clc=$native_build/src/asahi/clc/asahi_clc
if [[ ! -x $native_mesa_clc || ! -x $native_vtn_bindgen || \
      ! -x $native_asahi_clc ]]; then
    native_args=(
        -Dplatforms=
        -Dvulkan-drivers=
        -Dgallium-drivers=
        -Dtools=asahi
        -Dllvm=enabled
        -Dbuild-tests=false
        -Dvideo-codecs=
        -Dglx=disabled
        -Degl=disabled
        -Dgbm=disabled
        -Dshared-glapi=disabled
        -Dmesa-clc=enabled
        -Dprecomp-compiler=enabled
    )
    if [[ -f $native_build/build.ninja ]]; then
        meson setup --reconfigure "$native_build" "${native_args[@]}"
    else
        meson setup "$native_build" "$repo_root" "${native_args[@]}"
    fi
    meson compile -C "$native_build" mesa_clc vtn_bindgen2 asahi_clc
fi

export PATH=${native_mesa_clc:h}:${native_vtn_bindgen:h}:${native_asahi_clc:h}:$PATH
cross_args=(
    --cross-file "$cross_file"
    # The installable UMD is a production artifact.  Besides eliminating
    # debug-only loader-lock assertions in Mesa's generated-kernel registry,
    # this keeps hot renderer paths optimized.  The UMD glue itself carries
    # the target-scoped /W4 /WX-equivalent flags in d3d12/meson.build; making
    # unrelated upstream Mesa utility translation units globally fatal would
    # incorrectly expand this build's ownership boundary.
    -Dbuildtype=release
    -Db_ndebug=true
    -Dwarning_level=1
    -Dwerror=false
    -Dplatforms=windows
    -Dvulkan-drivers=
    -Dgallium-drivers=
    -Dasahi-d3d12=true
    -Dasahi-d3d12-wddm-version=$wddm_version
    -Dtools=
    -Dllvm=disabled
    -Dglx=disabled
    -Degl=disabled
    -Dgbm=disabled
    -Dshared-glapi=disabled
    -Dbuild-tests=false
    -Dvideo-codecs=
    -Dmesa-clc=system
    -Dprecomp-compiler=system
    -Dmin-windows-version=7
    # Override stale values from older cross-file revisions when this
    # script-owned Meson tree is reconfigured in place.
    -Dc_link_args=
    -Dcpp_link_args=
)
if [[ -f $cross_build/build.ninja ]]; then
    # Meson 1.11 no longer reliably infers the source directory when a
    # cross-file is regenerated between invocations.  Supplying the already
    # pinned source root is unambiguous and remains a non-destructive
    # reconfigure of this script-owned build directory.
    meson setup --reconfigure "$cross_build" "$repo_root" "${cross_args[@]}"
elif [[ -f $cross_build/meson-private/coredata.dat ]]; then
    # Recover only this script-owned build directory after an interrupted or
    # failed configure; Meson's wipe preserves the explicit source boundary.
    meson setup --wipe "$cross_build" "$repo_root" "${cross_args[@]}"
else
    meson setup "$cross_build" "$repo_root" "${cross_args[@]}"
fi
meson compile -C "$cross_build" d3d12agx d3d12agx_umd

com_dll=$cross_build/src/asahi/d3d12/d3d12agx.dll
umd_dll=$cross_build/src/asahi/d3d12/d3d12agx_umd.dll
python3 "$umd_dir/tests/check_pe.py" --readobj "$readobj" \
    --version "$driver_version" --filename d3d12agx_umd.dll "$umd_dll"
"$readobj" --file-headers "$com_dll" | grep -q 'IMAGE_FILE_MACHINE_ARM64'
print "built shared-core COM frontend: $com_dll"
print "built shared-core WDDM $wddm_version DDI frontend: $umd_dll"
