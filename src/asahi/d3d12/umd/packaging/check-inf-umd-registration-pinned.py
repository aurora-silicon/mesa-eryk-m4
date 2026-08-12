#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT
"""Run Aurora's current INF contract with an explicitly pinned DriverVer.

The native D3D12 UMD bundle may deliberately use a previously validated KMD
package while KMD bring-up advances in another working tree.  This wrapper
changes only the package-identity constant; it imports and runs every current
structural registration check from the caller-selected authoritative checker.
"""

import importlib.util
import os
import re
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"pinned INF registration checker: {message}")


base_text = os.environ.get("AURORA_INF_REGISTRATION_BASE", "")
expected = os.environ.get("AURORA_EXPECTED_DRIVER_VER", "")
if not re.fullmatch(r"\d{2}/\d{2}/\d{4},\d+\.\d+\.\d+\.\d+", expected):
    fail("AURORA_EXPECTED_DRIVER_VER is missing or malformed")

base = Path(base_text)
if not base.is_file():
    fail("AURORA_INF_REGISTRATION_BASE does not name a checker")

spec = importlib.util.spec_from_file_location(
    "aurora_inf_registration_contract", base
)
if spec is None or spec.loader is None:
    fail("could not load the authoritative checker")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
if not hasattr(module, "EXPECTED_DRIVER_VER") or not hasattr(module, "main"):
    fail("authoritative checker has an incompatible interface")
module.EXPECTED_DRIVER_VER = expected
module.main()
