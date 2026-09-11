#!/usr/bin/env python3
"""Generate exact dyadic FMA/clamp cases for the guarded raw-buffer oracle."""
import argparse
import struct
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
initial, expected = [], []
for i in range(256):
    x, y = ((i % 32) - 16) / 64, ((i // 32) - 4) / 8
    initial.extend([x, y, 0, 0, 0, 0, 0, 0])
    def clamp(value):
        return max(0, min(1, value))
    # Power-of-two scaling and these dyadic sums are all exactly representable
    # in binary32. Host arithmetic is an independent exact oracle here.
    expected.extend([x, y, clamp(-8*x+y), clamp(8*x+y),
                     clamp(.5*x-y), clamp(-.5*x-y), -8*x+y, 8*x+y])
(a.output/'input.bin').write_bytes(struct.pack('<2048f', *initial))
(a.output/'expected.bin').write_bytes(struct.pack('<2048f', *expected))
