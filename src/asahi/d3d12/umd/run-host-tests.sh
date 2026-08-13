#!/bin/zsh
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT

set -euo pipefail

umd_dir=${0:A:h}
d12_dir=${umd_dir:h}
repo_root=${umd_dir:h:h:h:h}
contract_dir=${AGX_WDDM_CONTRACT_CORE:-${repo_root:h}/AuroraSilicon/drivers/AppleAgxWddm/core}
out=${D12_UMD_HOST_TEST_OUT:-$repo_root/_build-d3d12-umd-host}
cc=${HOST_CC:-clang}

mkdir -p "$out"

common=(
    -std=c11
    -O2
    -Wall -Wextra -Werror -Wpedantic
)

for target in 20 21; do
    if [[ $target == 20 ]]; then
        interface=0x5002
    else
        interface=0x6003
    fi
    "$cc" "$common[@]" \
        -DD3D_UMD_INTERFACE_VERSION=$interface \
        -I"$contract_dir" -I"$umd_dir" \
        "$umd_dir/d12_umd_abi.c" "$umd_dir/tests/d12_umd_abi_tests.c" \
        -o "$out/d12_umd_abi_tests_wddm$target"
    "$out/d12_umd_abi_tests_wddm$target"
done

"$cc" "$common[@]" \
    "$d12_dir/d12_attachment_view_core.c" \
    "$d12_dir/tests/d12_attachment_view_core_tests.c" \
    -o "$out/d12_attachment_view_core_tests"
"$out/d12_attachment_view_core_tests"

"$cc" "$common[@]" \
    "$d12_dir/d12_core_caps.c" "$d12_dir/tests/d12_core_caps_tests.c" \
    -o "$out/d12_core_caps_tests"
"$out/d12_core_caps_tests"

"$cc" "$common[@]" \
    -I"$repo_root/src" -I"$repo_root/include" \
    "$d12_dir/d12_device_removal_core.c" \
    "$d12_dir/tests/d12_device_removal_core_tests.c" \
    -pthread \
    -o "$out/d12_device_removal_core_tests"
"$out/d12_device_removal_core_tests"

"$cc" "$common[@]" \
    "$d12_dir/d12_present_blt_math.c" \
    "$d12_dir/tests/d12_present_blt_math_tests.c" \
    -o "$out/d12_present_blt_math_tests"
"$out/d12_present_blt_math_tests"

"$cc" "$common[@]" \
    "$d12_dir/d12_query_core.c" \
    "$d12_dir/tests/d12_query_core_tests.c" \
    -o "$out/d12_query_core_tests"
"$out/d12_query_core_tests"

"$cc" "$common[@]" \
    "$d12_dir/d12_resolve_core.c" \
    "$d12_dir/tests/d12_resolve_core_tests.c" \
    -o "$out/d12_resolve_core_tests"
"$out/d12_resolve_core_tests"

"$cc" "$common[@]" \
    "$d12_dir/d12_write_set_core.c" \
    "$d12_dir/tests/d12_write_set_core_tests.c" \
    -o "$out/d12_write_set_core_tests"
"$out/d12_write_set_core_tests"

"$cc" "$common[@]" \
    "$d12_dir/d12_queue_timeline_core.c" \
    "$d12_dir/tests/d12_queue_timeline_core_tests.c" \
    -o "$out/d12_queue_timeline_core_tests"
"$out/d12_queue_timeline_core_tests"

"$cc" "$common[@]" \
    "$umd_dir/d12_umd_command_core.c" \
    "$umd_dir/tests/d12_umd_command_core_tests.c" \
    -o "$out/d12_umd_command_core_tests"
"$out/d12_umd_command_core_tests"

"$cc" "$common[@]" \
    "$umd_dir/d12_umd_lifetime_core.c" \
    "$umd_dir/tests/d12_umd_lifetime_core_tests.c" \
    -o "$out/d12_umd_lifetime_core_tests"
"$out/d12_umd_lifetime_core_tests"

"$cc" "$common[@]" \
    "$umd_dir/d12_umd_residency_core.c" \
    "$umd_dir/tests/d12_umd_residency_core_tests.c" \
    -o "$out/d12_umd_residency_core_tests"
"$out/d12_umd_residency_core_tests"

"$cc" "$common[@]" \
    -I"$d12_dir" -I"$umd_dir" \
    "$umd_dir/d12_umd_pipeline_serialization.c" \
    "$umd_dir/tests/d12_umd_pipeline_serialization_tests.c" \
    -o "$out/d12_umd_pipeline_serialization_tests"
"$out/d12_umd_pipeline_serialization_tests"

"$cc" "$common[@]" \
    -I"$d12_dir" -I"$umd_dir" \
    "$umd_dir/d12_umd_pipeline_serialization.c" \
    "$umd_dir/d12_umd_pipeline_library.c" \
    "$umd_dir/tests/d12_umd_pipeline_library_tests.c" \
    -o "$out/d12_umd_pipeline_library_tests"
"$out/d12_umd_pipeline_library_tests"

"$cc" "$common[@]" \
    -I"$contract_dir" -I"$d12_dir" -I"$umd_dir" \
    "$contract_dir/agxw_contract.c" \
    "$umd_dir/d12_umd_resource_private.c" \
    "$umd_dir/tests/d12_umd_resource_private_tests.c" \
    -o "$out/d12_umd_resource_private_tests"
"$out/d12_umd_resource_private_tests"

"$cc" "$common[@]" \
    -I"$repo_root/src/asahi/lib" -I"$contract_dir" \
    "$repo_root/src/asahi/lib/agx_d3dkmt_core.c" \
    "$contract_dir/agxw_contract.c" \
    "$repo_root/src/asahi/lib/agx_d3dkmt_core_tests.c" \
    -o "$out/agx_d3dkmt_core_tests"
"$out/agx_d3dkmt_core_tests"

"$cc" "$common[@]" \
    "$d12_dir/d12_dxbc_wrap.c" "$d12_dir/tests/d12_dxbc_wrap_tests.c" \
    -o "$out/d12_dxbc_wrap_tests"
"$out/d12_dxbc_wrap_tests" "$d12_dir/tests/shaders/cs.dxil"

python3 "$umd_dir/packaging/test_stage_package.py"
python3 "$umd_dir/tests/test_check_pe.py"
