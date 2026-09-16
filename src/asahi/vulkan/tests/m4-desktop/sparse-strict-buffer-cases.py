#!/usr/bin/env python3
"""Generate independent strict sparse SSBO read/write/atomic/rebind oracles.

Usage: sparse-strict-buffer-cases.py NEW_DIRECTORY
Run generated run.py OUTPUT ORACLE only under gpu-experiment.py's guard.
"""
import json
from pathlib import Path
import struct
import subprocess
import sys

root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=False)
half = 131072
initial = bytearray(half * 2)
for i in range(len(initial) // 4):
    struct.pack_into('<I', initial, i * 4, 0x10203040 + i)
(root / 'input.bin').write_bytes(initial)
cases = []
for op in ['load', 'vector', 'store', 'atomic', 'store-atomic']:
    body = {
        'load': 'dst.data[i]=uvec4(src.data[p],0,0,0);',
        'vector': 'dst.data[i]=uvec4(src.data[p],src.data[p+1],src.data[p+2],src.data[p+3]);',
        'store': 'src.data[p]=777u; memoryBarrierBuffer(); dst.data[i]=uvec4(src.data[p],0,0,0);',
        'atomic': 'uint old=atomicAdd(src.data[p],7u); memoryBarrierBuffer(); dst.data[i]=uvec4(old,src.data[p],0,0);',
        'store-atomic': 'src.data[p]=777u; memoryBarrierBuffer(); uint old=atomicAdd(src.data[p],7u); memoryBarrierBuffer(); dst.data[i]=uvec4(old,src.data[p],0,0);',
    }[op]
    # Disjoint coordinates, including three words at each page boundary.
    source = '#version 450\nlayout(local_size_x=64) in;\nlayout(set=0,binding=0,std430) coherent buffer In {uint data[];} src;\nlayout(set=0,binding=1,std430) writeonly buffer Out {uvec4 data[];} dst;\nvoid main(){uint i=gl_GlobalInvocationID.x;uint p=(i/32u)*4096u+(i%32u==31u?4093u:(i%32u)*128u);' + body + '}\n'
    (root / (op + '.comp')).write_text(source)
    subprocess.run(['glslangValidator', '-V', str(root / (op + '.comp')), '-o', str(root / (op + '.spv'))], check=True)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(root / (op + '.spv'))], check=True)
    for mask in [0, 0x55, 0xaa, 0xff]:
        for rebind in [False, True]:
            # Limit to seven pages so a four-word load never exceeds the view.
            name = f'{op}-{mask}-{int(rebind)}'
            expected = bytearray(initial)
            def execute(active):
                for i in range(224):
                    p = (i // 32) * 4096 + (4093 if i % 32 == 31 else (i % 32) * 128)
                    bound = bool(active & (1 << (p // 4096)))
                    value = struct.unpack_from('<I', expected, p * 4)[0] if bound else 0
                    if op == 'vector':
                        result = tuple(struct.unpack_from('<I', expected, (p+c)*4)[0] if active & (1 << ((p+c)//4096)) else 0 for c in range(4))
                    else:
                        if op in ['store', 'store-atomic']: value = 777 if bound else 0
                        new = value + 7 if bound and op in ['atomic', 'store-atomic'] else value
                        if bound and op != 'load': struct.pack_into('<I', expected, p*4, new)
                        result = (value, new, 0, 0) if 'atomic' in op else (value, 0, 0, 0)
                    struct.pack_into('<4I', expected, half+i*16, *result)
            if rebind: execute(mask ^ 0xff)
            execute(mask)
            (root / (name+'.bin')).write_bytes(expected)
            env = dict(M4_ORACLE_BINDING_COUNT='2', M4_ORACLE_SPARSE_SSBO='1', M4_ORACLE_SPARSE_TEXEL_MASK=str(mask ^ 0xff if rebind else mask))
            if rebind: env['M4_ORACLE_SPARSE_REBIND_MASK'] = str(mask)
            cases.append(dict(name=name, shader=op, env=env))
    # Each invocation uses exactly one unique coordinate; the extra 32 lanes
    # of the fourth workgroup return before touching either descriptor.
    source = source.replace('uint i=gl_GlobalInvocationID.x;', 'uint i=gl_GlobalInvocationID.x;if(i>=224u)return;')
    (root / (op+'.comp')).write_text(source)
    subprocess.run(['glslangValidator', '-V', str(root/(op+'.comp')), '-o', str(root/(op+'.spv'))], check=True)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(root/(op+'.spv'))], check=True)
(root/'cases.json').write_text(json.dumps(cases, indent=2)+'\n')
(root/'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
failed=0
for c in json.loads((r/'cases.json').read_text()):
 name=c['name'];print('STRICT_BUFFER_CASE:',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(c['shader']+'.spv')),str(r/'input.bin'),str(r/(name+'.bin')),'4',str(out/(name+'.bin'))],env=dict(os.environ,**c['env']),timeout=20,stdout=log,stderr=subprocess.STDOUT)
 print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True)
 failed+=p.returncode!=0
print(('FAIL' if failed else 'PASS')+': 40 strict sparse buffer configurations',flush=True)
sys.exit(bool(failed))
''')
print(root, len(cases))
