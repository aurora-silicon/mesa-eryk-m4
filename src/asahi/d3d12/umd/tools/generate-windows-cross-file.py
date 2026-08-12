#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT

"""Materialize the relocatable Windows ARM64 Meson cross-file template."""

from __future__ import annotations

import argparse
from pathlib import Path


def meson_string(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--toolchain", required=True, type=Path)
    parser.add_argument("--compat", required=True, type=Path)
    parser.add_argument("--contract", required=True, type=Path)
    parser.add_argument("--wdk", required=True, type=Path)
    parser.add_argument("--generated", required=True, type=Path)
    args = parser.parse_args()

    replacements = {
        "@C_COMPILER@": args.toolchain / "bin/aarch64-w64-mingw32-clang",
        "@CPP_COMPILER@": args.toolchain / "bin/aarch64-w64-mingw32-clang++",
        "@AR@": args.toolchain / "bin/llvm-ar",
        "@STRIP@": args.toolchain / "bin/llvm-strip",
        "@WINDRES@": args.toolchain / "bin/aarch64-w64-mingw32-windres",
        "@COMPAT_INCLUDE@": "-I" + str(args.compat.resolve()),
        "@AGX_WDDM_CONTRACT_CORE@": args.contract.resolve(),
        "@D3D12_UMD_WDK_INCLUDE@": args.wdk.resolve(),
        "@D3D12_UMD_GENERATED_INCLUDE@": args.generated.resolve(),
    }

    text = args.template.read_text(encoding="utf-8")
    for token, value in replacements.items():
        text = text.replace(token, meson_string(str(value)))
    if "@" in text:
        raise RuntimeError("unexpanded token remains in cross-file template")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
