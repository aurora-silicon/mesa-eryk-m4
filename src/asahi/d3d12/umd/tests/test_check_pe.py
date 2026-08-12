#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT

"""Unit tests for the installable UMD's loader-visible PE policy."""

import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).with_name("check_pe.py")
SPEC = importlib.util.spec_from_file_location("d12_check_pe", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
CHECK_PE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK_PE)


class CheckPeTests(unittest.TestCase):
    def test_accepts_arm64_single_export_and_system_imports(self) -> None:
        errors = CHECK_PE.validate_contract(
            "Machine: IMAGE_FILE_MACHINE_ARM64",
            "Export {\n  Name: OpenAdapter12\n}",
            "Import {\n Name: KERNEL32.dll\n}\n"
            "Import {\n Name: api-ms-win-crt-runtime-l1-1-0.dll\n}",
        )
        self.assertEqual(errors, [])

    def test_rejects_mingw_runtime_sidecar(self) -> None:
        errors = CHECK_PE.validate_contract(
            "Machine: IMAGE_FILE_MACHINE_ARM64",
            "Name: OpenAdapter12",
            "Name: KERNEL32.dll\nName: libwinpthread-1.dll",
        )
        self.assertEqual(len(errors), 1)
        self.assertIn("libwinpthread-1.dll", errors[0])

    def test_rejects_wrong_machine_or_export_surface(self) -> None:
        errors = CHECK_PE.validate_contract(
            "Machine: IMAGE_FILE_MACHINE_AMD64",
            "Name: OpenAdapter12\nName: DllGetClassObject",
            "Name: KERNEL32.dll",
        )
        self.assertEqual(len(errors), 2)
        self.assertIn("ARM64", errors[0])
        self.assertIn("DllGetClassObject", errors[1])

    def test_system_import_policy_is_case_insensitive(self) -> None:
        self.assertTrue(CHECK_PE.is_system_import("Kernel32.DLL"))
        self.assertTrue(CHECK_PE.is_system_import("EXT-MS-WIN-FOO.DLL"))
        self.assertFalse(CHECK_PE.is_system_import("libgcc_s_seh-1.dll"))


if __name__ == "__main__":
    unittest.main()
