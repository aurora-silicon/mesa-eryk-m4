#!/usr/bin/env python3
"""Read an explicitly typed FlatBuffer field without inventing a schema.

Example: flatbuffer-field.py vertex.bin --path 1.3 --type u32
Paths are zero-based table field indices. Intermediate fields must be tables.
Keep the input and native compiler manifest; values alone do not prove hardware.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('input', type=Path)
p.add_argument('--path', required=True)
p.add_argument('--type', choices=['u8', 'u16', 'u32', 'u64'], required=True)
a = p.parse_args()
d = a.input.read_bytes()

def read(fmt, offset):
    if not 0 <= offset <= len(d) - struct.calcsize(fmt):
        raise ValueError('Field outside buffer')
    return struct.unpack_from(fmt, d, offset)[0]

table = read('<I', 0)
indices = [int(x) for x in a.path.split('.')]
if any(i < 0 for i in indices):
    p.error('Field indices must be nonnegative')
steps = []
for n, index in enumerate(indices):
    vtable = table - read('<i', table)
    size = read('<H', vtable)
    if size < 4 or size % 2 or vtable + size > len(d):
        raise ValueError('Invalid vtable')
    field = 4 + index * 2
    relative = read('<H', vtable + field) if field < size else 0
    if not relative:
        raise ValueError(f'Field {index} absent; its schema default is unknown')
    offset = table + relative
    if relative >= read('<H', vtable + 2):
        raise ValueError('Field outside table')
    steps.append(dict(table=table, field=index, offset=offset))
    if n + 1 < len(indices):
        table = offset + read('<I', offset)
formats = dict(u8='<B', u16='<H', u32='<I', u64='<Q')
print(json.dumps(dict(input=str(a.input), sha256=hashlib.sha256(d).hexdigest(),
                     path=a.path, type=a.type, value=read(formats[a.type], offset),
                     steps=steps), indent=2))
