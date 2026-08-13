#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT
"""Prove that every usable pinned baseline D3D12 DDI slot is installed."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


TABLES = {
    "D3D12DDI_DEVICE_FUNCS_CORE_0003": 89,
    "D3D12DDI_DEVICE_FUNCS_CORE_0010": 91,
    "D3D12DDI_COMMAND_LIST_FUNCS_3D_0003": 51,
    "D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001": 7,
}
FIELD = re.compile(r"\b(?:PFN[A-Z0-9_]+|void\*)\s+(pfn[A-Za-z0-9_]+)\s*;")
INSTALLER = re.compile(
    r"\b(d12_umd_install_[A-Za-z0-9_]+)\s*\(\s*"
    r"(D3D12DDI_[A-Za-z0-9_]+)\s*\*\s*table\s*\)\s*\{",
    re.S,
)
ASSIGNMENT = re.compile(r"\btable->(pfn[A-Za-z0-9_]+)\s*=")


def extract_struct(header: str, name: str) -> list[str]:
    match = re.search(
        rf"typedef\s+struct\s+{name}\s*\{{(.*?)\}}\s*{name}\s*;",
        header,
        re.S,
    )
    if not match:
        raise ValueError(f"missing pinned WDK table {name}")
    return FIELD.findall(match.group(1))


def function_body(text: str, opening_brace: int) -> str:
    depth = 0
    for index in range(opening_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening_brace + 1 : index]
    raise ValueError("unterminated DDI installer")


def installed_fields(source_dir: Path) -> dict[str, set[str]]:
    installed = {name: set() for name in TABLES}
    for path in source_dir.glob("*.c"):
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in INSTALLER.finditer(text):
            table_type = match.group(2)
            if table_type not in installed:
                continue
            body = function_body(text, match.end() - 1)
            installed[table_type].update(ASSIGNMENT.findall(body))

    # DDI 0010 is constructed from the compatible 0003 prefix/suffix and then
    # receives its release-1 pipeline-library functions. Field names shared by
    # those copied ranges therefore inherit the baseline installation proof.
    installed["D3D12DDI_DEVICE_FUNCS_CORE_0010"].update(
        installed["D3D12DDI_DEVICE_FUNCS_CORE_0003"]
    )
    return installed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--source-dir", required=True, type=Path)
    args = parser.parse_args()

    header = args.header.read_text(encoding="utf-8", errors="replace")
    installed = installed_fields(args.source_dir)
    summaries: list[str] = []
    failed = False

    for table, expected_count in TABLES.items():
        fields = extract_struct(header, table)
        if len(fields) != expected_count:
            print(
                f"FAIL: {table} has {len(fields)} slots; expected "
                f"{expected_count} from pinned WDK 26100.6584"
            )
            failed = True
        usable = {field for field in fields if not field.startswith("pfnUnused")}
        missing = sorted(usable - installed[table])
        if missing:
            print(f"FAIL: {table} leaves generated traps in: {' '.join(missing)}")
            failed = True
        summaries.append(f"{table}={len(usable)}/{len(fields)}")

    if failed:
        return 1
    print("check_ddi_table_coverage.py: PASS (" + ", ".join(summaries) + ")")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
