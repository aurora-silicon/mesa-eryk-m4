#!/usr/bin/env python3
"""Keep the evidence-backed AGX2 IR -> AGX3 mnemonic map fail-closed."""

import sys
from pathlib import Path

COMPILER = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(COMPILER))

from agx_opcodes import opcodes


assert opcodes["mov_imm"].agx3_mnemonic == "ldimm"
assert opcodes["stop"].agx3_mnemonic == "end"
assert opcodes["threadgroup_barrier"].agx3_mnemonic == "barrier"
assert sum(op.agx3_mnemonic is not None for op in opcodes.values()) == 3
print("agx3 opcode mapping: mov_imm -> ldimm, stop -> end, "
      "threadgroup_barrier -> barrier; all other opcodes fail closed")
