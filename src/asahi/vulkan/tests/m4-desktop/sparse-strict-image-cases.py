#!/usr/bin/env python3
"""Generate strict sparse image reads, stores and atomics with two 64 KiB tiles.

Usage: sparse-strict-image-cases.py NEW_DIRECTORY
Run run.py OUTPUT ORACLE only under gpu-experiment.py's mandatory guard.
"""
import json
from pathlib import Path
import struct
import subprocess
import sys
root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=False)
half = 256 * 128 * 4
initial = bytearray(half * 2)
for i in range(len(initial) // 4):
    struct.pack_into('<I', initial, i * 4, 0x10203040 + i)
(root / 'input.bin').write_bytes(initial)
cases = []
for op in ['fetch', 'sparse', 'load', 'store', 'atomic', 'store-atomic']:
    storage = op not in ['fetch', 'sparse']
    decl = ('layout(set=0,binding=0,r32ui) uniform coherent uimage2D tex;' if storage
            else 'layout(set=0,binding=0) uniform usampler2D tex;')
    body = {'fetch': 'uvec4 v=texelFetch(tex,p,0); dst.data[i]=v;',
            'sparse': 'uvec4 v; int code=sparseTexelFetchARB(tex,p,0,v); dst.data[i]=uvec4(v.x,uint(sparseTexelsResidentARB(code)),v.y,v.w);',
            'load': 'dst.data[i]=imageLoad(tex,p);',
            'store': 'imageStore(tex,p,uvec4(777)); memoryBarrierImage(); dst.data[i]=imageLoad(tex,p);',
            'atomic': 'uint old=imageAtomicAdd(tex,p,7u); memoryBarrierImage(); dst.data[i]=uvec4(old,imageLoad(tex,p).x,0,1);',
            'store-atomic': 'imageStore(tex,p,uvec4(777)); memoryBarrierImage(); uint old=imageAtomicAdd(tex,p,7u); memoryBarrierImage(); dst.data[i]=uvec4(old,imageLoad(tex,p).x,0,1);'}[op]
    source = '#version 450\n#extension GL_ARB_sparse_texture2 : require\nlayout(local_size_x=64) in;\n' + decl + '\nlayout(set=0,binding=1,std430) writeonly buffer Out {uvec4 data[];} dst;\nvoid main(){uint i=gl_GlobalInvocationID.x;ivec2 p=ivec2((i%16u)*16u,(i/16u)*8u);' + body + '}\n'
    (root / (op + '.comp')).write_text(source)
    subprocess.run(['glslangValidator', '-V', str(root / (op + '.comp')), '-o', str(root / (op + '.spv'))], check=True)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(root / (op + '.spv'))], check=True)
    for mask in range(4):
        name = f'{op}-{mask}'
        expected = bytearray(initial)
        for i in range(256):
            x, y = (i % 16) * 16, (i // 16) * 8
            bound = bool(mask & (1 << (x // 128)))
            value = 0x10203040 + y * 256 + x if bound else 0
            if op == 'sparse': result = (value, int(bound), 0, 1)
            elif op == 'store': result = (777 if bound else 0, 0, 0, 1)
            elif op in ['atomic', 'store-atomic']:
                old = 777 if bound and op == 'store-atomic' else value
                result = (old, old + 7 if bound else 0, 0, 1)
            else: result = (value, 0, 0, 1)
            struct.pack_into('<4I', expected, half + i * 16, *result)
        (root / (name + '.bin')).write_bytes(expected)
        env = dict(M4_ORACLE_BINDING_COUNT='2', M4_ORACLE_IMAGE_FORMAT='98',
                   M4_ORACLE_SPARSE_IMAGE_MASK=str(mask), M4_ORACLE_RESIDENCY='1')
        if storage: env['M4_ORACLE_STORAGE_IMAGE'] = '1'
        cases.append(dict(name=name, shader=op, env=env))
(root / 'cases.json').write_text(json.dumps(cases, indent=2) + '\n')
(root / 'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
failed=0
for c in json.loads((r/'cases.json').read_text()):
 name=c['name'];print('STRICT_IMAGE_CASE:',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(c['shader']+'.spv')),str(r/'input.bin'),str(r/(name+'.bin')),'4',str(out/(name+'.bin'))],env=dict(os.environ,**c['env']),timeout=20,stdout=log,stderr=subprocess.STDOUT)
 print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True)
 failed+=p.returncode!=0
print(('FAIL' if failed else 'PASS')+': 24 strict sparse image configurations',flush=True)
sys.exit(bool(failed))
''')
print(root, len(cases))
