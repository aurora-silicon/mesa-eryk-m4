#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT

"""Verify the native ARM64 DLL's loader-visible contract.

The UMD is loaded by the graphics runtime out of DriverStore.  Any MinGW or
compiler-runtime DLL import that happens to exist in a developer shell makes
the package non-deployable, so this check treats the import table as part of
the ABI alongside architecture and exports.
"""

import argparse
import subprocess
import sys


SYSTEM_DLLS = {
    "advapi32.dll",
    "bcrypt.dll",
    "cfgmgr32.dll",
    "crypt32.dll",
    "d3d12.dll",
    "dxgi.dll",
    "gdi32.dll",
    "kernel32.dll",
    "ntdll.dll",
    "ole32.dll",
    "oleaut32.dll",
    "sechost.dll",
    "setupapi.dll",
    "shell32.dll",
    "user32.dll",
    "uuid.dll",
    "version.dll",
    "winmm.dll",
    "ws2_32.dll",
}


def tool_output(tool: str, *arguments: str) -> str:
    return subprocess.run(
        [tool, *arguments],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    ).stdout


def parse_names(output: str) -> list[str]:
    """Return llvm-readobj Name fields without depending on indentation."""
    names = []
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("Name:"):
            names.append(stripped.partition(":")[2].strip())
    return names


def is_system_import(name: str) -> bool:
    normalized = name.lower()
    return normalized in SYSTEM_DLLS or normalized.startswith(
        ("api-ms-win-", "ext-ms-win-")
    )


def validate_contract(headers: str, exports: str, imports: str) -> list[str]:
    errors = []
    if "IMAGE_FILE_MACHINE_ARM64" not in headers:
        errors.append("not a native ARM64 PE image")

    export_names = parse_names(exports)
    if export_names != ["OpenAdapter12"]:
        errors.append(f"unexpected export surface: {export_names!r}")

    non_system = [
        name for name in parse_names(imports) if not is_system_import(name)
    ]
    if non_system:
        errors.append(
            "non-system DLL imports make the UMD non-self-contained: "
            f"{non_system!r}"
        )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--readobj", required=True)
    parser.add_argument("dll")
    args = parser.parse_args()

    headers = tool_output(args.readobj, "--file-headers", args.dll)
    exports = tool_output(args.readobj, "--coff-exports", args.dll)
    imports = tool_output(args.readobj, "--coff-imports", args.dll)

    errors = validate_contract(headers, exports, imports)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(
        "check_pe.py: PASS (ARM64, OpenAdapter12 only, system imports only)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
