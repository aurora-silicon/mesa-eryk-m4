#!/usr/bin/env python3
"""Generate 64 strict sparse R64 storage-image read/write/atomic probes.

Usage: atomic64-sparse-image-cases.py NEW_DIRECTORY
Run generated run.py OUTPUT ORACLE under gpu-experiment.py. Its --reference
mode checks the 14 ordinary, fully-backed operation cases on lavapipe.
"""
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=False)
width, height = 256, 64
words = width * height
mask64 = (1 << 64) - 1
rng = random.Random(0x6405a)
values = [rng.getrandbits(64) for _ in range(words)]
edge = [0, mask64, 1 << 63, (1 << 63)-1, 0xffffffff, 0x100000000,
        0x123456789abcdef, 0xfedcba9876543210]
for i in range(128):
    x, y = (i % 16) * 16, (i // 16) * 4
    values[y * width + x] = edge[i % len(edge)]
initial = values + [0xdeadc0dedeadc0de] * words
(root / 'input.bin').write_bytes(struct.pack('<' + str(len(initial)) + 'Q', *initial))
operand = 0x12345678abcdef00
signed = lambda v: v if v < 1 << 63 else v - (1 << 64)
cases = []
for sint in [False, True]:
    ty, vec, image, fmt = ('int64_t', 'i64vec4', 'i64image2D', 'r64i') if sint else ('uint64_t', 'u64vec4', 'u64image2D', 'r64ui')
    for op in ['load', 'sparse', 'store', 'add', 'min', 'max', 'exchange', 'compare_swap']:
        shader = ('signed-' if sint else 'unsigned-') + op
        read = f'{ty} old = imageLoad(tex,p).x;'
        if op == 'sparse': read = f'{vec} value; int code=sparseImageLoadARB(tex,p,value); {ty} old=value.x;'
        elif op == 'store': read += f'imageStore(tex,p,{vec}(arg));'
        elif op not in ['load']:
            fn = {'compare_swap':'CompSwap'}.get(op, op.title())
            args = '(i&1u)==0u?imageLoad(tex,p).x:~imageLoad(tex,p).x,arg' if op == 'compare_swap' else 'arg'
            read = f'{ty} old=imageAtomic{fn}(tex,p,{args});'
        second = f'{ty}(sparseTexelsResidentARB(code))' if op == 'sparse' else 'imageLoad(tex,p).x'
        source = f'''#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_image_int64 : require
#extension GL_ARB_sparse_texture2 : require
layout(local_size_x=64) in;
layout(set=0,binding=0,{fmt}) uniform coherent {image} tex;
layout(set=0,binding=1,std430) buffer Out {{{vec} data[];}} dst;
void main() {{
 uint i=gl_GlobalInvocationID.x;
 ivec2 p=ivec2((i%16u)*16u,(i/16u)*4u);
 {ty} arg={ty}(0x{operand:x}ul);
 {read}
 memoryBarrierImage(); dst.data[i]={vec}(old,{second},0,1);
}}
'''
        (root / (shader + '.comp')).write_text(source)
        subprocess.run(['glslangValidator','-V',str(root/(shader+'.comp')),'-o',str(root/(shader+'.spv'))],check=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(root/(shader+'.spv'))],check=True)
        for mask in range(4):
            name = f'{shader}-{mask}'
            expected = list(initial)
            for i in range(256):
                x, y = (i % 16) * 16, (i // 16) * 4
                bound = bool(mask & (1 << (x // 128)))
                old = values[y * width + x] if bound else 0
                result = old
                if op == 'sparse': result = int(bound)
                elif bound:
                    if op in ['store', 'exchange']: result = operand
                    elif op == 'add': result = (old + operand) & mask64
                    elif op in ['min','max']:
                        a, b = (signed(old), signed(operand)) if sint else (old, operand)
                        result = (min(a,b) if op == 'min' else max(a,b)) & mask64
                    elif op == 'compare_swap': result = operand if i % 2 == 0 else old
                expected[words+i*4:words+i*4+4] = [old,result,0,1]
            (root/(name+'.bin')).write_bytes(struct.pack('<'+str(len(expected))+'Q',*expected))
            env = dict(M4_ORACLE_BINDING_COUNT='2',M4_ORACLE_STORAGE_IMAGE='1',
                       M4_ORACLE_IMAGE_FORMAT='111' if sint else '110',
                       M4_ORACLE_IMAGE_ATOMIC64='1',M4_ORACLE_SPARSE_IMAGE_MASK=str(mask))
            if op == 'sparse': env['M4_ORACLE_RESIDENCY']='1'
            cases.append(dict(name=name,shader=shader,mask=mask,operation=op,env=env))
(root/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
(root/'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
reference='--reference' in sys.argv[3:];failed=0;count=0
for c in json.loads((r/'cases.json').read_text()):
 if reference and (c['mask']!=3 or c['operation']=='sparse'):continue
 env=dict(c['env'])
 if reference:
  env.pop('M4_ORACLE_SPARSE_IMAGE_MASK');env['M4_ORACLE_IMAGE_EXTENT']='256x64'
 name=c['name'];count+=1;print('SPARSE_IMAGE64_CASE:',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(c['shader']+'.spv')),str(r/'input.bin'),str(r/(name+'.bin')),'4',str(out/(name+'.bin'))],env=dict(os.environ,**env),stdout=log,stderr=subprocess.STDOUT,timeout=30)
 print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True);failed+=p.returncode!=0
print(('FAIL' if failed else 'PASS')+f': {count} sparse image64 configurations',flush=True)
sys.exit(bool(failed))
''')
print(root,len(cases))
