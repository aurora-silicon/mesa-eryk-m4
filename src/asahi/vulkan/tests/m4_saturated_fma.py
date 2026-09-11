#!/usr/bin/env python3
"""Generate exact dyadic FMA/clamp cases for the guarded raw-buffer oracle."""
import argparse
import struct
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
p.add_argument('--mixed', action='store_true', help='Generate mixed F32/F16 saturated multiply cases')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
if a.mixed:
    initial, expected = bytearray(), bytearray()
    for i in range(256):
        x = ((i % 32) - 16) / 8
        h0, h1 = ((i // 32) - 4) / 4, ((i // 32) + 1) / 4
        inputs = struct.pack('<fee', x, h0, h1)
        initial += inputs + bytes(8)
        expected += inputs + struct.pack('<ff', max(0, min(1, x*h0)), max(0, min(1, x*h1)))
    (a.output/'input.bin').write_bytes(initial)
    (a.output/'expected.bin').write_bytes(expected)
    raise SystemExit(0)
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
