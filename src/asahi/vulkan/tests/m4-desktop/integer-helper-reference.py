#!/usr/bin/env python3
"""Write the exact 5120-byte oracle for m4_vulkan_integer_helpers.comp.

Uses Python integer multiplication/counting and IEEE binary16 packing, with
explicit 32-bit wrapping and signed interpretation. Does not use any GPU.
"""
import argparse
import struct
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
args = parser.parse_args()
rg = bytearray()
red = bytearray()


def signed(x):
    return x if x < 0x80000000 else x - (1 << 32)


for pixel in range(1024):
    i = pixel >> 3
    a = ((i * 2654435761) & 0xffffffff) ^ 0x80000000
    b = ((i * 2246822519) & 0xffffffff) ^ 0xffffffff
    values = [(a * b) >> 32,
              ((signed(a) * signed(b)) >> 32) & 0xffffffff,
              max(a - b, 0), a.bit_count()]
    word = values[(pixel >> 1) & 3] >> ((pixel & 1) * 16)
    rg += struct.pack('<ee', word & 255, (word >> 8) & 255)
    red.append(b.bit_count())

with args.output.open('xb') as output:
    output.write(rg + red)
