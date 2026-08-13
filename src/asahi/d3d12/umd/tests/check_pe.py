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
import struct
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


def validate_version_resource(
    image: bytes, expected_version: str, expected_filename: str
) -> list[str]:
    """Validate fixed and localized fields in a PE VERSIONINFO resource."""
    errors = []
    try:
        major, minor, patch, build = (
            int(component) for component in expected_version.split(".")
        )
    except (TypeError, ValueError):
        return [f"invalid expected version: {expected_version!r}"]

    components = (major, minor, patch, build)
    if any(component < 0 or component > 0xFFFF for component in components):
        return [
            f"expected version component is outside UINT16: {expected_version}"
        ]

    signature = struct.pack("<I", 0xFEEF04BD)
    expected_ms = (major << 16) | minor
    expected_ls = (patch << 16) | build
    fixed_match = False
    start = 0
    while True:
        offset = image.find(signature, start)
        if offset < 0:
            break
        if offset + 16 <= len(image):
            _, _, file_ms, file_ls = struct.unpack_from("<IIII", image, offset)
            if file_ms == expected_ms and file_ls == expected_ls:
                fixed_match = True
                break
        start = offset + 1
    if not fixed_match:
        errors.append(
            f"missing VS_FIXEDFILEINFO FILEVERSION {expected_version}"
        )

    for field, value in (
        ("FileVersion", expected_version),
        ("ProductVersion", expected_version),
        ("OriginalFilename", expected_filename),
    ):
        field_bytes = field.encode("utf-16-le")
        value_bytes = value.encode("utf-16-le")
        if field_bytes not in image or value_bytes not in image:
            errors.append(f"missing VERSIONINFO {field}={value!r}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--readobj", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--filename", required=True)
    parser.add_argument("dll")
    args = parser.parse_args()

    headers = tool_output(args.readobj, "--file-headers", args.dll)
    exports = tool_output(args.readobj, "--coff-exports", args.dll)
    imports = tool_output(args.readobj, "--coff-imports", args.dll)

    errors = validate_contract(headers, exports, imports)
    with open(args.dll, "rb") as image_file:
        errors += validate_version_resource(
            image_file.read(), args.version, args.filename
        )
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(
        "check_pe.py: PASS (ARM64, OpenAdapter12 only, system imports only, "
        f"VERSIONINFO {args.version})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
