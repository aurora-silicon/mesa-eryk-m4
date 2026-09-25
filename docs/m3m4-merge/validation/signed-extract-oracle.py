#!/usr/bin/env python3
"""Prepare exact signed-bitfield tests for vk-buffer-oracle (no GPU execution).

Covers every legal constant 32-bit offset/width pair, plus runtime pairs,
against a Python sign-extension reference. Run under the target GPU guard.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
pairs = [(offset, width) for width in range(1, 33)
         for offset in range(33 - width)]
values = [0, 1, 0xffffffff, 0x7fffffff, 0x80000000, 0x55555555, 0xaaaaaaaa]
values += [1 << bit for bit in range(32)]
values += [((byte << 24) | (byte << 16) | (byte << 8) | byte)
           for byte in range(256)]
rng = random.Random(0x32bfe)
values += [rng.getrandbits(32) for _ in range(512 - len(values))]
stride = 4 + len(pairs) + 1
source = ['#version 450', 'layout(local_size_x=32) in;',
          'layout(set=0,binding=0,std430) buffer B {uint w[];};',
          f'void main(){{uint base=gl_GlobalInvocationID.x*{stride}u;',
          'int x=int(w[base]);']
for i, (offset, width) in enumerate(pairs):
    source.append(f'w[base+{4+i}u]=uint(bitfieldExtract(x,{offset},{width}));')
source += [f'w[base+{stride-1}u]=uint(bitfieldExtract(x,int(w[base+1]),int(w[base+2])));', '}']
(a.output / 'probe.comp').write_text('\n'.join(source) + '\n')
def extract(value, offset, width):
    if not width:
        return 0
    field = (value >> offset) & ((1 << width) - 1)
    signed = field - (1 << width) if field & (1 << (width-1)) else field
    return signed & 0xffffffff
initial, expected = [], []
for i, value in enumerate(values):
    offset, width = pairs[i % len(pairs)] if i % 17 else (0, 0)
    header = [value, offset, width, 0xdecafbad]
    initial += header + [0xdeadbeef] * (stride - 4)
    expected += header + [extract(value, o, w) for o, w in pairs] + [extract(value, offset, width)]
for name, words in [('input', initial), ('expected', expected)]:
    (a.output / (name + '.bin')).write_bytes(struct.pack('<' + 'I' * len(words), *words))
cmd = ['glslangValidator', '-V', str(a.output/'probe.comp'), '-o', str(a.output/'probe.spv')]
r = subprocess.run(cmd, capture_output=True, text=True)
(a.output/'compile.log').write_text(r.stdout+r.stderr)
r.check_returncode()
manifest = dict(lanes=len(values), constant_fields=len(pairs), workgroups=len(values)//32,
                output_words=len(expected), gpu_executed=False,
                reference='Python integer extraction and two-complement sign extension',
                sha256={f.name: hashlib.sha256(f.read_bytes()).hexdigest()
                        for f in a.output.iterdir() if f.is_file()})
(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest))
