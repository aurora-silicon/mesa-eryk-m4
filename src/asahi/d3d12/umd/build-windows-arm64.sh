#!/bin/zsh
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT
#
# Strict native ARM64 cross-build for the D3D12 WDDM UMD adapter milestone.

set -euo pipefail

umd_dir=${0:A:h}
d12_dir=${umd_dir:h}
repo_root=${umd_dir:h:h:h:h}
asahi_workspace=${AURORA_ASAHI_WORKSPACE:-/Users/dj/Developer/Asahi/windows-on-apple-silicon}
contract_dir=${AGX_WDDM_CONTRACT_CORE:-${repo_root:h}/AuroraSilicon/drivers/AppleAgxWddm/core}
wdk_root=${WDK_ROOT:-$asahi_workspace/build/deps/wdk-arm64-26100.6584/package/c}
sdk_root=${SDK_ROOT:-$asahi_workspace/build/deps/windows-sdk-cpp-26100.6584/package/c}
wdk_include=$wdk_root/Include/10.0.26100.0
sdk_include=$sdk_root/Include/10.0.26100.0
toolchain=${LLVM_MINGW_ROOT:-/Users/dj/Toolchains/llvm-mingw}
cc=${CC:-$toolchain/bin/aarch64-w64-mingw32-clang}
readobj=${LLVM_READOBJ:-/opt/homebrew/opt/llvm/bin/llvm-readobj}
windres=${WINDRES:-$toolchain/bin/aarch64-w64-mingw32-windres}
out=${D12_UMD_BUILD_OUT:-$repo_root/_build-d3d12-umd-arm64}
staged=$out/winddk
wddm_target=${D12_UMD_WDDM_TARGET:-21}

case $wddm_target in
    20) umd_interface=0x5002; driver_version=20.17.240.0 ;;
    21) umd_interface=0x6003; driver_version=21.17.240.0 ;;
    23) umd_interface=0x8001; driver_version=23.17.240.0 ;;
    24) umd_interface=0x9001; driver_version=24.17.240.0 ;;
    *) print -u2 "D12_UMD_WDDM_TARGET must be 20, 21, 23, or 24"; exit 2 ;;
esac

for required in "$cc" "$readobj" "$windres" \
    "$wdk_include/um/d3d12umddi.h" \
    "$contract_dir/agxw_contract.h" \
    "$contract_dir/agxw_contract.c"; do
    [[ -e $required ]] || { print -u2 "missing dependency: $required"; exit 2; }
done

mkdir -p "$out/obj" "$staged"
mkdir -p "$out/generated"

for header in d3d12umddi.h d3d10umddi.h d3dumddi.h \
    d3d10TokenizedProgramFormat.hpp d3d11TokenizedProgramFormat.hpp \
    d3d12TokenizedProgramFormat.hpp; do
    cp "$wdk_include/um/$header" "$staged/"
done
cp "$wdk_include/shared/d3dkmddi.h" "$staged/"
for header in d3dkmthk.h d3dkmdt.h d3dukmdt.h; do
    cp "$sdk_include/shared/$header" "$staged/"
done
for header in dxgiddi.h dxmini.h; do
    cp "$sdk_include/um/$header" "$staged/"
done
python3 "$umd_dir/tools/staticize-wdk-c-constants.py" \
    "$staged/d3d10umddi.h"
python3 "$umd_dir/tools/staticize-wdk-c-constants.py" \
    "$staged/d3d12umddi.h"
python3 "$umd_dir/tools/generate-ddi-tables.py" \
    --header "$staged/d3d12umddi.h" \
    --output "$out/generated/d12_umd_tables_generated.inc"
python3 "$umd_dir/tests/check_ddi_table_coverage.py" \
    --header "$staged/d3d12umddi.h" \
    --source-dir "$umd_dir"

cflags=(
    -std=c11
    -O2
    -fno-strict-aliasing
    -Wall -Wextra -Werror -Wpedantic
    -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wundef
    -Wno-unknown-pragmas
    -Wno-ignored-attributes
    -Wno-microsoft-enum-forward-reference
    -Wno-language-extension-token
    -D_WIN32_WINNT=0x0A00
    -DNTDDI_VERSION=0x0A000010
    -DD3D_UMD_INTERFACE_VERSION=$umd_interface
    # WDK headers are MSVC-oriented and are treated as system headers so
    # strict diagnostics remain fatal for Aurora source, not Microsoft's C
    # extensions (zero-length arrays and UINT32-valued enumerators).
    -isystem "$staged"
    -I"$contract_dir"
    -I"$repo_root/src"
    -I"$d12_dir"
    -I"$umd_dir"
    -I"$out/generated"
)

sources=(
    "$contract_dir/agxw_contract.c"
    "$d12_dir/d12_core_caps.c"
    "$d12_dir/d12_dxbc_wrap.c"
    "$d12_dir/d12_present_blt_math.c"
    "$d12_dir/d12_present_format_core.c"
    "$d12_dir/d12_query_core.c"
    "$d12_dir/d12_resolve_core.c"
    "$umd_dir/d12_umd_abi.c"
    "$umd_dir/d12_umd_kmt.c"
    "$umd_dir/d12_umd_agx_transport.c"
    "$umd_dir/d12_umd_command_core.c"
    "$umd_dir/d12_umd_lifetime_core.c"
    "$umd_dir/d12_umd_command_funcs.c"
    "$umd_dir/d12_umd_pipeline_serialization.c"
    "$umd_dir/d12_umd_pipeline_library.c"
    "$umd_dir/d12_umd_state_funcs.c"
    "$umd_dir/d12_umd_pipeline_funcs.c"
    "$umd_dir/d12_umd_queue_runtime.c"
    "$umd_dir/d12_umd_residency.c"
    "$umd_dir/d12_umd_residency_core.c"
    "$umd_dir/d12_umd_resource_private.c"
    "$umd_dir/d12_umd_device_funcs.c"
    "$umd_dir/d12_umd_descriptor_funcs.c"
    "$umd_dir/d12_umd_query_funcs.c"
    "$umd_dir/d12_umd_resource_command_funcs.c"
    "$umd_dir/d12_umd_present_funcs.c"
    "$umd_dir/d12_umd_tables.c"
    "$umd_dir/d12_umd_adapter.c"
)
objects=()
for source in "$sources[@]"; do
    object=$out/obj/${source:t:r}.o
    "$cc" "$cflags[@]" -c "$source" -o "$object"
    objects+=("$object")
done

"$windres" --define="AURORA_WDDM_MAJOR=${driver_version%%.*}" \
    --input-format=rc --output-format=coff \
    "$umd_dir/d3d12agx_umd.rc" "$out/obj/d3d12agx_umd_version.o"
objects+=("$out/obj/d3d12agx_umd_version.o")

"$cc" "$cflags[@]" -c "$umd_dir/tests/d12_umd_winabi_tests.c" \
    -o "$out/obj/d12_umd_winabi_tests.o"

"$cc" -shared -o "$out/d3d12agx_umd.dll" "$objects[@]" \
    "$umd_dir/d3d12agx_umd.def" -Wl,--kill-at -lkernel32

"$cc" "$cflags[@]" \
    "$contract_dir/agxw_contract.c" \
    "$umd_dir/d12_umd_resource_private.c" \
    "$umd_dir/d12_umd_pipeline_serialization.c" \
    "$umd_dir/tests/d12_umd_ddiprobe.c" \
    -municode -o "$out/d12_umd_ddiprobe.exe" -lkernel32

"$cc" "$cflags[@]" \
    "$umd_dir/d12_umd_abi.c" "$umd_dir/tests/d12_umd_hwprobe.c" \
    -municode -o "$out/d12_umd_hwprobe.exe" -lgdi32 -lkernel32

"$cc" "$cflags[@]" \
    -idirafter "$sdk_include/um" -idirafter "$sdk_include/shared" \
    "$umd_dir/d12_umd_abi.c" "$umd_dir/tests/d12_umd_runtime_probe.c" \
    -o "$out/d12_umd_runtime_probe.exe" \
    -ld3d12 -ldxgi -ldxguid -luuid -lgdi32 -lole32 -lkernel32

"$cc" "$cflags[@]" \
    "$contract_dir/agxw_contract.c" "$umd_dir/d12_umd_kmt.c" \
    "$umd_dir/tests/d12_umd_kmtprobe.c" \
    -o "$out/d12_umd_kmtprobe.exe" -lkernel32

"$cc" "$cflags[@]" \
    "$contract_dir/agxw_contract.c" "$umd_dir/d12_umd_kmt.c" \
    "$umd_dir/d12_umd_queue_runtime.c" \
    "$umd_dir/tests/d12_umd_queueprobe.c" \
    -o "$out/d12_umd_queueprobe.exe" -lkernel32

"$cc" "$cflags[@]" \
    "$repo_root/src/asahi/lib/agx_d3dkmt_runtime.c" \
    "$umd_dir/d12_umd_agx_transport.c" \
    "$umd_dir/tests/d12_umd_agx_transport_probe.c" \
    -o "$out/d12_umd_agx_transport_probe.exe" -lkernel32

python3 "$umd_dir/tests/check_pe.py" --readobj "$readobj" \
    --version "$driver_version" --filename d3d12agx_umd.dll \
    "$out/d3d12agx_umd.dll"

print "built $out/d3d12agx_umd.dll (WDDM $wddm_target UMD ABI $umd_interface)"
