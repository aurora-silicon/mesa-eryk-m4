#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT

"""Keep the branch-local Apple GPU ledger aligned with enum agx_chip."""

from pathlib import Path
import re
import sys


def fail(message: str) -> None:
    raise SystemExit(f"apple-gpu-feature-matrix: {message}")


def chip_names(header: str) -> list[str]:
    match = re.search(
        r"enum agx_chip\s*\{(?P<body>.*?)AGX_CHIP_UNKNOWN", header, re.S
    )
    if match is None:
        fail("could not find enum agx_chip")

    names = re.findall(r"\bAGX_CHIP_(G[0-9]+[A-Z])\b", match.group("body"))
    if not names or len(names) != len(set(names)):
        fail("enum agx_chip is empty or contains duplicate named identities")
    return names


def table_rows(document: str, heading: str) -> list[list[str]]:
    marker = f"## {heading}\n"
    start = document.find(marker)
    if start < 0:
        fail(f"missing {heading!r} section")

    lines = document[start + len(marker) :].splitlines()
    rows: list[list[str]] = []
    in_table = False
    for line in lines:
        if line.startswith("## "):
            break
        if not line.startswith("|"):
            if in_table and rows:
                break
            continue

        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if cells[0] in ("Mesa identity", "---") or set(cells[0]) == {"-"}:
            in_table = True
            continue
        if in_table:
            rows.append(cells)

    if not rows:
        fail(f"missing data rows in {heading!r}")
    return rows


def validate_table(
    rows: list[list[str]], expected: list[str], columns: int, heading: str
) -> None:
    names = [row[0] for row in rows]
    if names != expected:
        fail(f"{heading!r} identities differ from enum order: {names!r}")

    for row in rows:
        if len(row) != columns:
            fail(
                f"{heading!r} row {row[0]} has {len(row)} columns, "
                f"expected {columns}"
            )
        if any(not cell for cell in row):
            fail(f"{heading!r} row {row[0]} contains an empty cell")


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: test-feature-matrix.py AGX_CHIP_HEADER FEATURE_MATRIX")

    header = Path(sys.argv[1]).read_text(encoding="utf-8")
    document = Path(sys.argv[2]).read_text(encoding="utf-8")
    expected = chip_names(header)

    hardware = table_rows(document, "Hardware and encoding axes")
    api = table_rows(document, "Mesa API and kernel relationship")
    validate_table(hardware, expected, 7, "Hardware and encoding axes")
    validate_table(api, expected, 6, "Mesa API and kernel relationship")

    status = re.compile(r"\*\*(?:RS|IV|RE|FC)(?:/(?:RS|IV|RE|FC))*\*\*")
    for row in hardware:
        for cell in row[3:]:
            if status.search(cell) is None:
                fail(f"hardware row {row[0]} has an unclassified capability cell")
    for row in api:
        for cell in row[1:]:
            if status.search(cell) is None:
                fail(f"API/kernel row {row[0]} has an unclassified capability cell")

    print(
        "apple-gpu-feature-matrix: PASS "
        f"({len(expected)} identities, {len(hardware) + len(api)} rows)"
    )


if __name__ == "__main__":
    main()
