#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT
"""Add the native D3D12 UMD to an AppleAgxWddm package staging directory.

Run this after AppleAgxWddm.inf/.cdf and the existing render bundle have been
copied into the stage, but before MakeCat.  It deliberately edits only the
throwaway package stage: the active Aurora KMD source tree may be under an
independent paging/display bring-up at the same time.
"""

from __future__ import annotations

import argparse
import re
import shutil
import struct
from pathlib import Path

DLL_NAME = "d3d12agx_umd.dll"
DISPLAY_NAME = "d3d12agx_umd"
ARM64_MACHINE = 0xAA64


def require_arm64_pe(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError(f"{path} is not a PE image")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset > len(data) - 6 or data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"{path} has an invalid PE header")
    machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
    if machine != ARM64_MACHINE:
        raise ValueError(
            f"{path} has PE machine 0x{machine:04x}, expected ARM64 0xaa64"
        )


def append_section_member(text: str, section: str, member: str) -> str:
    pattern = re.compile(
        rf"(?im)^(\[{re.escape(section)}\][^\r\n]*\r?\n)(.*?)(?=^\[|\Z)",
        re.DOTALL,
    )
    match = pattern.search(text)
    if not match:
        raise ValueError(f"missing [{section}] section")
    body = match.group(2)
    name = member.split("=", 1)[0].split(",", 1)[0].strip().lower()
    for line in body.splitlines():
        existing = line.split(";", 1)[0].split("=", 1)[0].split(",", 1)[0]
        if existing.strip().lower() == name:
            if member.lower() in line.lower():
                return text
            raise ValueError(f"[{section}] already contains conflicting {name}")
    newline = "\r\n" if "\r\n" in text else "\n"
    replacement = match.group(1) + body
    if replacement and not replacement.endswith(("\n", "\r")):
        replacement += newline
    replacement += member + newline
    return text[: match.start()] + replacement + text[match.end() :]


def append_multisz(text: str, key: str, value: str) -> str:
    pattern = re.compile(
        rf"(?im)^(\s*HKR\s*,\s*,\s*{re.escape(key)}\s*,"
        rf"\s*%REG_MULTI_SZ%\s*,)([^\r\n]*)(\r?\n|$)"
    )
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        raise ValueError(f"expected exactly one {key} REG_MULTI_SZ row")
    match = matches[0]
    entries = [entry.strip().strip('"') for entry in match.group(2).split(",")]
    if any(not entry for entry in entries):
        raise ValueError(f"{key} contains an empty REG_MULTI_SZ entry")
    if value.lower() in (entry.lower() for entry in entries):
        return text
    if len(entries) != 3:
        raise ValueError(
            f"{key} has {len(entries)} entries before DX12; expected DX9/DX10/DX11"
        )
    replacement = match.group(1) + match.group(2) + "," + value + match.group(3)
    return text[: match.start()] + replacement + text[match.end() :]


def patch_inf(text: str) -> str:
    text = append_section_member(text, "SourceDisksFiles", f"{DLL_NAME} = 1,,")
    text = append_section_member(text, "AppleAgxWddm_UMD", f"{DLL_NAME},,,0")
    text = append_multisz(text, "InstalledDisplayDrivers", DISPLAY_NAME)
    text = append_multisz(text, "UserModeDriverName", rf"%13%\{DLL_NAME}")
    return text


def patch_cdf(text: str) -> str:
    return append_section_member(
        text, "CatalogFiles", f"<HASH>{DLL_NAME}={DLL_NAME}"
    )


def stage(package_dir: Path, dll: Path) -> None:
    package_dir = package_dir.resolve()
    dll = dll.resolve()
    inf = package_dir / "AppleAgxWddm.inf"
    cdf = package_dir / "AppleAgxWddm.cdf"
    if not package_dir.is_dir() or not inf.is_file() or not cdf.is_file():
        raise ValueError(
            "package stage must contain AppleAgxWddm.inf and AppleAgxWddm.cdf"
        )
    require_arm64_pe(dll)

    inf_text = patch_inf(inf.read_text(encoding="utf-8"))
    cdf_text = patch_cdf(cdf.read_text(encoding="utf-8"))
    inf.write_text(inf_text, encoding="utf-8", newline="")
    cdf.write_text(cdf_text, encoding="utf-8", newline="")
    shutil.copy2(dll, package_dir / DLL_NAME)

    stale_catalog = package_dir / "AppleAgxWddm.cat"
    if stale_catalog.exists():
        stale_catalog.unlink()
    print(f"staged {DLL_NAME} in {package_dir}")
    print("regenerate and sign AppleAgxWddm.cat before installing the package")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", type=Path, required=True)
    parser.add_argument("--dll", type=Path, required=True)
    args = parser.parse_args()
    stage(args.package_dir, args.dll)


if __name__ == "__main__":
    main()
