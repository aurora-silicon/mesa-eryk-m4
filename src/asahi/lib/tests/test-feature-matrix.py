#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT

"""Keep the deliberately small M3-M5/A18 Pro ledger complete and ordered."""

from pathlib import Path
import re
import sys


TARGETS = [
    "G15G", "G15S", "G15C", "G15D",
    "G16G", "G16S", "G16C", "G17P",
    "G17G", "G17S", "G17C",
]


def fail(message: str) -> None:
    raise SystemExit(f"apple-gpu-feature-matrix: {message}")


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: test-feature-matrix.py AGX_CHIP_HEADER FEATURE_MATRIX")

    header = Path(sys.argv[1]).read_text(encoding="utf-8")
    document = Path(sys.argv[2]).read_text(encoding="utf-8")

    enum_match = re.search(
        r"enum agx_chip\s*\{(?P<body>.*?)AGX_CHIP_UNKNOWN", header, re.S
    )
    if enum_match is None:
        fail("could not find enum agx_chip")
    enum_chips = set(
        re.findall(r"\bAGX_CHIP_(G[0-9]+[A-Z])\b", enum_match.group("body"))
    )
    if any(chip not in enum_chips for chip in TARGETS):
        fail("target identity is missing from enum agx_chip")

    rows = []
    for line in document.splitlines():
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if cells[0] in ("Mesa chip", "---") or set(cells[0]) == {"-"}:
            continue
        rows.append(cells)

    if [row[0] for row in rows] != TARGETS:
        fail("rows must contain only the 11 scoped targets in enum order")

    status = re.compile(r"\*\*(?:IV|RE|FC)(?:/(?:IV|RE|FC))*\*\*")
    for row in rows:
        if len(row) != 7 or any(not cell for cell in row):
            fail(f"row {row[0]} must contain seven non-empty cells")
        if any(status.search(cell) is None for cell in row[2:]):
            fail(f"row {row[0]} has an unclassified capability")

    print("apple-gpu-feature-matrix: PASS (11 scoped identities)")


if __name__ == "__main__":
    main()
