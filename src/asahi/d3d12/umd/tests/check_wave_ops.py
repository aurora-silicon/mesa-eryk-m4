#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT

from pathlib import Path


root = Path(__file__).resolve().parents[2]
nir = (root / "dxil/d12_dxil_nir.c").read_text()

required = (
    "D12_DXOP_WAVE_ACTIVE_BIT = 120",
    "case D12_DXOP_WAVE_ACTIVE_BIT:",
    "case 0: op = nir_op_iand;",
    "case 1: op = nir_op_ior;",
    "case 2: op = nir_op_ixor;",
    "nir_reduce(b, get_def(ctx, p[0]), .reduction_op = op)",
    "nir_vote_feq(b, 1, get_def(ctx, p[0]))",
    "nir_vote_ieq(b, 1, get_def(ctx, p[0]))",
    "wave all-equal operand is out of range",
)

for token in required:
    assert token in nir, f"DXIL wave lowering omits {token}"

assert "D12_DXOP_WAVE_ACTIVE_BIT = 119" not in nir
assert "D12_DXOP_WAVE_ACTIVE_BIT = 121" not in nir

print("wave-ops: PASS (bitwise reductions and typed all-equal votes)")
