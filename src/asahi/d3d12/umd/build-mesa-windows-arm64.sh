#!/bin/zsh
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT
#
# Reproducibly cross-build both native AGX D3D12 frontends against the one
# renderer-core archive: the public COM DLL and the WDDM UMD DDI DLL.

set -euo pipefail

umd_dir=${0:A:h}
d12_dir=${umd_dir:h}
repo_root=${umd_dir:h:h:h:h}
workspace_root=${repo_root:h}
asahi_workspace=${AURORA_ASAHI_WORKSPACE:-/Users/dj/Developer/Asahi/windows-on-apple-silicon}
contract_dir=${AGX_WDDM_CONTRACT_CORE:-$workspace_root/AuroraSilicon/drivers/AppleAgxWddm/core}
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
    *) print -u2 "D12_UMD_WDDM_TARGET must be 20, 21, or 23"; exit 2 ;;
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
