#!/usr/bin/env python3
"""Build a native EOT BTNF input from the witnessed Apple flatbuffer schema.

RT arguments are native format:component-count pairs, in attachment order.
Examples: 12:4 9:1 (RGBA16F + R16_UINT), 12:4 12:2 9:1 (three targets).
Compile the result with native-metal-compile.py --stage builtin. No GPU runs.
"""
import argparse
import struct
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('rt', nargs='+')
dim=p.add_mutually_exclusive_group()
dim.add_argument('--array', action='store_true', help='Native type3 layered 2D attachment')
dim.add_argument('--multisample', action='store_true', help='Native type4 multisampled 2D attachment')
p.add_argument('--split-partitions', action='store_true',
               help='Put each attachment in a separate tile partition (diagnostic)')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
try:
    formats = [tuple(map(int, v.split(':'))) for v in a.rt]
    assert 1 <= len(formats) <= 8
    assert all(len(v) == 2 and 0 <= v[0] <= 255 and 1 <= v[1] <= 4 for v in formats)
except (ValueError, AssertionError):
    p.error('Use one to eight FORMAT:COMPONENTS pairs, with 1..4 components')
# BTNF root, builtin union and EOT table. Field0 of EOT points to the vector;
# field1 is the active attachment mask. Match retained single/dual inputs.
header = bytes.fromhex('''
14 00 00 00 42 54 4e 46 0c 00 18 00 04 00 10 00
11 00 14 00 0c 00 00 00 00 00 00 00 03 00 00 00
00 00 00 00 01 05 00 00 0c 00 00 00 08 00 0c 00
04 00 08 00 08 00 00 00 04 00 00 00 0c 00 00 00
08 00 0a 00 04 00 08 00 08 00 00 00 08 00 00 00
''')
n = len(formats)
data = bytearray(header + struct.pack('<II', (1 << n) - 1, n))
vtable = len(data) + n * 4
first = vtable + 20
for i in range(n):
    data += struct.pack('<I', first + 12 * i - len(data))
data += struct.pack('<9H', 18, 11, 4, 5, 6, 7, 8, 9, 10) + b'\0\0'
for i, (fmt, components) in enumerate(formats):
    data += struct.pack('<I7B', len(data) - vtable, i if a.split_partitions else 0, fmt, 4 if a.multisample else 3 if a.array else 2, 0, 0, components, 0)
    if i + 1 < n:
        data += b'\0'
a.output.parent.mkdir(parents=True, exist_ok=True)
with a.output.open('xb') as f:
    f.write(data)
print(f'{a.output}: {n} attachments, {len(data)} bytes; no GPU execution')
