#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT

from __future__ import annotations

import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("stage_package.py")
SPEC = importlib.util.spec_from_file_location("stage_package", MODULE_PATH)
assert SPEC and SPEC.loader
stage_package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stage_package)


class StagePackageTests(unittest.TestCase):
    def test_patches_fourth_runtime_slot_and_catalog(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            inf = root / "AppleAgxWddm.inf"
            cdf = root / "AppleAgxWddm.cdf"
            dll = root / "source.dll"
            inf.write_text(
                "[SourceDisksFiles]\nAppleAgxWddm.sys = 1,,\n\n"
                "[AppleAgxWddm_UMD]\nAuroraD3D11Triton.dll,,,0\n\n"
                "[AppleAgxWddm_SoftwareDeviceSettings]\n"
                "HKR,, InstalledDisplayDrivers, %REG_MULTI_SZ%, A,A,A\n"
                "HKR,, UserModeDriverName, %REG_MULTI_SZ%, %13%\\A.dll,"
                "%13%\\A.dll,%13%\\A.dll\n",
                encoding="utf-8",
            )
            cdf.write_text(
                "[CatalogFiles]\n<HASH>AppleAgxWddm.inf=AppleAgxWddm.inf\n",
                encoding="utf-8",
            )
            image = bytearray(0x100)
            image[:2] = b"MZ"
            struct.pack_into("<I", image, 0x3C, 0x80)
            image[0x80:0x84] = b"PE\0\0"
            struct.pack_into("<H", image, 0x84, stage_package.ARM64_MACHINE)
            dll.write_bytes(image)

            stage_package.stage(root, dll)
            patched = inf.read_text(encoding="utf-8")
            self.assertIn("d3d12agx_umd.dll = 1,,", patched)
            self.assertIn("d3d12agx_umd.dll,,,0", patched)
            self.assertIn("A,A,A,d3d12agx_umd", patched)
            self.assertIn(r"%13%\d3d12agx_umd.dll", patched)
            self.assertIn(
                "<HASH>d3d12agx_umd.dll=d3d12agx_umd.dll",
                cdf.read_text(encoding="utf-8"),
            )
            self.assertTrue((root / "d3d12agx_umd.dll").is_file())

            # Idempotence matters when a package build is resumed.
            stage_package.stage(root, dll)
            self.assertEqual(
                inf.read_text(encoding="utf-8").count("d3d12agx_umd.dll"), 3
            )


if __name__ == "__main__":
    unittest.main()
