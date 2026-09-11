#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate 1024 signed/unsigned product cases for tools/vk-buffer-oracle.c."""
import itertools
from pathlib import Path
import random
import struct
import sys

out = Path(sys.argv[1])
out.mkdir(exist_ok=False, parents=True)
edges = [0, 1, 2, 0xffff, 0x10000, 0x7fffffff, 0x80000000,
         0xfffffffe, 0xffffffff, 0x12345678]
cases = list(itertools.product(edges, repeat=2))
rng = random.Random(0x8132)
cases += [(rng.getrandbits(32), rng.getrandbits(32))
          for _ in range(1024 - len(cases))]
initial, expected = bytearray(), bytearray()
for x, y in cases:
    initial += struct.pack('<6I', x, y, *([0xdeadc0de] * 4))
    unsigned = x * y
    signed = (x if x < 2**31 else x - 2**32) * (y if y < 2**31 else y - 2**32)
    expected += struct.pack('<6I', x, y, unsigned & 0xffffffff,
                            unsigned >> 32, signed & 0xffffffff,
                            (signed >> 32) & 0xffffffff)
(out / 'input.bin').write_bytes(initial)
(out / 'expected.bin').write_bytes(expected)
print('1024 cases, 6144 words; dispatch 32 workgroups of 32 threads')
