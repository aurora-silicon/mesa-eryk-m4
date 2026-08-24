#!/usr/bin/env python3
"""Static gate for 64-bit buffer-view arithmetic and complete access bounds."""

from pathlib import Path


d12 = Path(__file__).resolve().parents[2]
nir = (d12 / "dxil/d12_dxil_nir.c").read_text()


def function(name: str, next_name: str) -> str:
    start = nir.index(name)
    end = nir.index(next_name, start)
    return nir[start:end]


status = function("buffer_residency_status(", "buffer_atomic_address(")
load = function("robust_load_buffer_vec4(", "robust_store_buffer_masked(")
store = function("robust_store_buffer_masked(", "Map a patch-constant semantic")

for body in (load, store):
    assert "nir_def *component_last = nir_uadd_sat(" in body
    assert "nir_ult(b, component_last, size64)" in body
    assert "nir_ult(b, component_offset, size64)" not in body

assert "nir_ult(b, last_requested, view_size)" in status
assert "nir_iand(b, valid, nir_ior(b, nir_inot(b, has_map), mapped))" in status
assert "nir_umin(b, last_requested" not in status

print(
    "large-buffer-views: PASS "
    "(64-bit byte extents + full-width robustness/residency)"
)
