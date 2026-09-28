#!/usr/bin/env python3
"""Keep the evidence-backed AGX2 IR -> AGX3 mnemonic map fail-closed."""

import sys
from pathlib import Path

COMPILER = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(COMPILER))

from agx_opcodes import opcodes


# Keep an explicit whitelist, including the native IR operations added for
# sampler descriptors, packed colour and arithmetic. A count alone both
# rejects valid additions and misses a substitution with the same count.
expected = {
    "mov_imm": "ldimm",
    "fltnorm_agx3": "fltnorm",
    "normflt_agx3": "normflt",
    "imul_high_div3_agx3": "imadd",
    "stop": "end",
    "threadgroup_barrier": "barrier",
    "yield_agx3": "yield",
    "scoped_barrier_agx3": "barrier",
    "scs_load_agx3": "ldscs",
    "scs_store_agx3": "stscs",
    "scs_load_uniform_indexed_agx3": "ldscs",
    "scs_store_uniform_indexed_agx3": "stscs",
    "pack_rgb10a2_agx3": "pack",
    "unpack_rgb10a2_agx3": "unpack",
    "pack_min_lod_agx3": "pack_smpir",
    "pack_layer_agx3": "pack_smpir",
    "pack_sample_agx3": "pack_smpir",
    "pack_lod_offset_agx3": "pack_smpir",
}
actual = {name: op.agx3_mnemonic for name, op in opcodes.items()
          if op.agx3_mnemonic is not None}
assert actual == expected, (actual, expected)
print(f"agx3 opcode mapping: all {len(expected)} named mappings match; "
      "unlisted operations have no direct mnemonic mapping")
