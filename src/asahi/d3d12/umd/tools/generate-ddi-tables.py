#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT
"""Generate baseline D3D12 DDI table bindings from the pinned WDK header."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


TABLES = (
    (
        "D3D12DDI_DEVICE_FUNCS_CORE_0003",
        "D12_UMD_DEVICE_CORE_ENTRY_COUNT",
        "d12_umd_fill_device_core_traps",
        "device",
        None,
    ),
    (
        "D3D12DDI_COMMAND_LIST_FUNCS_3D_0003",
        "D12_UMD_COMMAND_LIST_ENTRY_COUNT",
        "d12_umd_fill_command_list_traps",
        "command_list",
        None,
    ),
    (
        "D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001",
        "D12_UMD_COMMAND_QUEUE_ENTRY_COUNT",
        "d12_umd_fill_command_queue_traps",
        "command_queue",
        None,
    ),
    (
        "D3D12DDI_DEVICE_FUNCS_CORE_0099",
        "D12_UMD_DEVICE_CORE_ENTRY_COUNT_0099",
        "d12_umd_fill_device_core_traps_0099",
        "device",
        "D12_UMD_TARGETS_WDDM31",
    ),
    (
        "D3D12DDI_COMMAND_LIST_FUNCS_3D_0099",
        "D12_UMD_COMMAND_LIST_ENTRY_COUNT_0099",
        "d12_umd_fill_command_list_traps_0099",
        "command_list",
        "D12_UMD_TARGETS_WDDM31",
    ),
)

FIELD = re.compile(r"^\s*(PFN[A-Z0-9_]+|void\*)\s+(pfn[A-Za-z0-9_]+)\s*;", re.M)
TYPEDEF = re.compile(
    r"typedef\s+(?:_Check_return_\s+)?"
    r"(?P<return>VOID|void\s*\*|void|HRESULT|SIZE_T|UINT|BOOL|"
    r"D3D12DDI_CPU_DESCRIPTOR_HANDLE|D3D12DDI_GPU_DESCRIPTOR_HANDLE|"
    r"D3D12DDI_HEAP_AND_RESOURCE_SIZES|D3D12DDI_GPU_VIRTUAL_ADDRESS|"
    r"D3DKMT_HANDLE|D3D12DDI_DRIVER_MATCHING_IDENTIFIER_STATUS)\s*"
    r"\(\s*APIENTRY(?:\s+CALLBACK)?\s*\*\s*(?P<name>PFN[A-Z0-9_]+)\s*\)",
    re.M,
)

TRAPS = {
    "device": {
        "VOID": "d12_umd_trap_void",
        "void": "d12_umd_trap_void",
        "HRESULT": "d12_umd_trap_hresult",
        "SIZE_T": "d12_umd_trap_size",
        "UINT": "d12_umd_trap_uint",
        "D3D12DDI_CPU_DESCRIPTOR_HANDLE": "d12_umd_trap_cpu_descriptor",
        "D3D12DDI_GPU_DESCRIPTOR_HANDLE": "d12_umd_trap_gpu_descriptor",
        "D3D12DDI_HEAP_AND_RESOURCE_SIZES": "d12_umd_trap_heap_resource_sizes",
        "D3D12DDI_GPU_VIRTUAL_ADDRESS": "d12_umd_trap_gpu_virtual_address",
        "D3DKMT_HANDLE": "d12_umd_trap_kmt_handle",
        "void*": "d12_umd_trap_pointer",
        "D3D12DDI_DRIVER_MATCHING_IDENTIFIER_STATUS":
            "d12_umd_trap_matching_identifier",
    },
    "command_list": {
        "VOID": "d12_umd_command_list_trap_void",
        "void": "d12_umd_command_list_trap_void",
    },
    "command_queue": {
        "VOID": "d12_umd_command_queue_trap_void",
        "void": "d12_umd_command_queue_trap_void",
    },
}


def extract_struct(text: str, name: str) -> list[tuple[str, str]]:
    match = re.search(
        rf"typedef\s+struct\s+{name}\s*\{{(.*?)\}}\s*{name}\s*;", text, re.S
    )
    if not match:
        raise SystemExit(f"missing WDK table {name}")
    fields = FIELD.findall(match.group(1))
    if not fields:
        raise SystemExit(f"empty WDK table {name}")
    return fields


def generate(header: Path) -> str:
    text = header.read_text(encoding="utf-8", errors="replace")
    returns = {m.group("name"): m.group("return") for m in TYPEDEF.finditer(text)}
    out = [
        "/* GENERATED from pinned d3d12umddi.h. DO NOT EDIT. */",
        "/* clang-format off */",
        "#if defined(__clang__)",
        "#pragma clang diagnostic push",
        '#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"',
        "#endif",
    ]

    for struct, count_macro, filler, family, guard in TABLES:
        fields = extract_struct(text, struct)
        if guard:
            out.extend(("", f"#if {guard}"))
        out.extend(("", f"#define {count_macro} {len(fields)}u", "", "static void"))
        out.append(f"{filler}({struct} *table)")
        out.append("{")
        for pfn, field in fields:
            if pfn == "void*":
                out.append(f"   table->{field} = NULL;")
                continue
            ret = returns.get(pfn)
            if ret is None:
                raise SystemExit(f"{pfn}: return type not found")
            trap = TRAPS[family].get(ret)
            if trap is None:
                raise SystemExit(f"{pfn}: unsupported {family} return type {ret}")
            if family == "device" and field.startswith("pfnDestroy") and ret in (
                "VOID",
                "void",
            ):
                trap = "d12_umd_trap_destroy_void"
            out.append(f"   table->{field} = ({pfn}){trap};")
        out.append("}")
        if guard:
            out.append("#endif")

    out.extend(
        (
            "",
            "#if defined(__clang__)",
            "#pragma clang diagnostic pop",
            "#endif",
            "/* clang-format on */",
            "",
        )
    )
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate(args.header), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
