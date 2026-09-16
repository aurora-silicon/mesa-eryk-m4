#!/usr/bin/env python3
"""Generate exact SSBO oracles for AGX scratch lifetime and register spilling.

This prepares files only. Execute probe.spv with vk-buffer-oracle through
gpu-experiment.py on M4, adding AGX_MESA_DEBUG=spill for the spill case.
"""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('case', choices=['repeat', 'spill', 'dynstore'])
p.add_argument('--vectors', type=int, default=8, choices=range(2, 65))
p.add_argument('--atomic-return', action='store_true',
               help='Keep a device atomic return live across scratch reads')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
n = 32 if a.case in ('repeat', 'dynstore') else a.vectors
stride = n + 1 if a.case == 'repeat' else n + 2 if a.case == 'dynstore' else n * 2
body = [f'uint b=gl_GlobalInvocationID.x*{stride};']
if a.case == 'dynstore':
    # Static initialisation, three lane-dependent dynamic stores (one a
    # read-modify-write), then static reads of fixed and of every stored
    # location and a dynamic read; the compiler cannot resolve the indices.
    body += ['uvec4 a[32];']
    body += [f'a[{i}]=w[b+{i}];' for i in range(n)]
    body += ['uint i0=w[b+32].x&31, i1=w[b+32].y&31, i2=w[b+32].z&31;',
             'a[i0]=w[b+32]^uvec4(1,2,3,4);',
             'a[i1]=a[i1]+uvec4(7);',
             'a[i2]^=a[i0];',
             'uvec4 r=uvec4(0);',
             'r+=a[0]+a[5]+a[17]+a[31];',
             'r^=a[i0]^a[i1]^a[i2]^a[(i0+1)&31];',
             'w[b+33]=r;']
elif a.case == 'repeat':
    body += ['uvec4 a[32];']
    body += [f'a[{i}]=w[b+{i}];' for i in range(n)]
    body += ['uint index=w[b+32].x&31;',
             'w[b+32]=a[index]^a[0]^a[5]^a[31];']
else:
    body += [f'uvec4 v{i}=w[b+{i}];' for i in range(n)]
    body += [f'w[b+{n+i}]=v{i}^uvec4(0xa55aa55a);' for i in range(n)]
if a.atomic_return:
    # The private values must survive a device atomic, whose returned value
    # shares TPS storage with private arrays and register allocator spills.
    body.insert(n + (2 if a.case in ('repeat', 'dynstore') else 1),
                'uint old=atomicAdd(w[b].x,7u);')
    body += ['w[b].y=old;']
source = '''#version 450
layout(local_size_x=32) in;
layout(set=0,binding=0,std430) coherent volatile buffer B{uvec4 w[];};
void main(){
''' + '\n'.join(body) + '\n}\n'
(a.output / 'probe.comp').write_text(source)
initial, expected = [], []
for lane in range(32):
    values = [(0x90001234 + lane * 811 + i * 101) & 0xffffffff
              for i in range(n * 4)]
    if a.case == 'dynstore':
        i0, i1, i2 = (lane * 7 + 3) & 31, (lane * 5 + 11) & 31, (lane * 13 + 1) & 31
        ctrl = [i0, i1, i2, 0xc0de0000 + lane]
        M = 0xffffffff
        arr = [values[i*4:i*4+4] for i in range(n)]
        arr[i0] = [(ctrl[c] ^ (c + 1)) & M for c in range(4)]
        arr[i1] = [(v + 7) & M for v in arr[i1]]
        arr[i2] = [arr[i2][c] ^ arr[i0][c] for c in range(4)]
        r = [(arr[0][c] + arr[5][c] + arr[17][c] + arr[31][c]) & M for c in range(4)]
        r = [r[c] ^ arr[i0][c] ^ arr[i1][c] ^ arr[i2][c] ^ arr[(i0 + 1) & 31][c] for c in range(4)]
        initial += values + ctrl + [0, 0, 0, 0]
        expected += values + ctrl + r
    elif a.case == 'repeat':
        initial += values + [lane, 0, 0, 0]
        expected += values + [values[lane*4+c] ^ values[c] ^
                              values[20+c] ^ values[124+c] for c in range(4)]
    else:
        initial += values + [0] * len(values)
        expected += values + [v ^ 0xa55aa55a for v in values]
if a.atomic_return:
    for lane in range(32):
        base = lane * stride * 4
        expected[base] = (initial[base] + 7) & 0xffffffff
        expected[base + 1] = initial[base]
for name, words in [('input', initial), ('reference', expected)]:
    (a.output / (name + '.bin')).write_bytes(struct.pack('<'+'I'*len(words), *words))
command = ['glslangValidator', '-V', str(a.output/'probe.comp'),
           '-o', str(a.output/'probe.spv')]
compiled = subprocess.run(command, text=True, capture_output=True)
(a.output/'compile.log').write_text(compiled.stdout + compiled.stderr)
compiled.check_returncode()
manifest = {'case': a.case, 'vectors': n, 'words': len(initial),
            'atomic_return': a.atomic_return,
            'workgroups': [1, 1, 1], 'command': command,
            'gpu_executed': False, 'sha256': {
                f.name: hashlib.sha256(f.read_bytes()).hexdigest()
                for f in a.output.iterdir() if f.is_file()}}
(a.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(json.dumps(manifest, indent=2))
