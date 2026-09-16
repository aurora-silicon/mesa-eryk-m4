#!/usr/bin/env python3
"""Generate unsigned 64-bit min/max atomic value/contention oracles.

Usage: atomic64-minmax-cases.py NEW_DIRECTORY
Run generated run.py OUTPUT ORACLE only under the mandatory hardware guard.
"""
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=False)
rng = random.Random(0x6416)
values = [rng.getrandbits(64) for _ in range(1024)]
edges = [0, 0xffffffffffffffff, 0x8000000000000000, 0x7fffffff00000000,
         0xffffffff, 0x80000000ffffffff, 0x123456789abcdef0, 0xfedcba9876543210]
for i in range(32): values[i] = edges[i % len(edges)]
for i in range(32): values[256+i] = edges[(i+3) % len(edges)]
# Both single-address operations must change memory, including on a noop shim.
values[0] = 0x89abcdef12345678
(root/'input.bin').write_bytes(struct.pack('<1024Q', *values))
cases = []
for op in ['min', 'max']:
    for mode in ['unique', 'single', 'striped']:
        name = op+'-'+mode
        index = {'unique':'i', 'single':'0u', 'striped':'(i&7u)'}[mode]
        expected = list(values)
        for i in range(256):
            p = i if mode == 'unique' else 0 if mode == 'single' else i & 7
            expected[p] = (min if op == 'min' else max)(expected[p], values[256+i])
        (root/(name+'.bin')).write_bytes(struct.pack('<1024Q', *expected))
        source = '''#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_atomic_int64 : require
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) buffer Data {uint64_t data[];} src;
void main(){uint i=gl_GlobalInvocationID.x;atomicOP(src.data[INDEX],src.data[256u+i]);}
'''.replace('OP', op.title()).replace('INDEX', index)
        (root/(name+'.comp')).write_text(source)
        subprocess.run(['glslangValidator','-V',str(root/(name+'.comp')),'-o',str(root/(name+'.spv'))], check=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(root/(name+'.spv'))],check=True)
        cases.append(name)
(root/'cases.json').write_text(json.dumps(cases)+'\n')
(root/'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
failed=0
for name in json.loads((r/'cases.json').read_text()):
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(name+'.spv')),str(r/'input.bin'),str(r/(name+'.bin')),'4',str(out/(name+'.bin'))],env=dict(os.environ,M4_ORACLE_ATOMIC64='1'),stdout=log,stderr=subprocess.STDOUT,timeout=20)
 print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True);failed+=p.returncode!=0
print(('FAIL' if failed else 'PASS')+': six unsigned atomic64 minmax configurations',flush=True)
sys.exit(bool(failed))
''')
print(root, len(cases))
