#!/usr/bin/env python3
"""Generate integer64 storage-image operation, returned-value and readback probes.

Usage: atomic64-image-cases.py NEW_DIRECTORY
Run the generated runner only through the mandatory hardware experiment guard.
"""
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=False)
rng = random.Random(0x641a6e)
mask = (1 << 64) - 1
edge = [0, mask, 1 << 63, (1 << 63)-1, 0xffffffff, 0x100000000,
        0x0123456789abcdef, 0xfedcba9876543210]
values = edge * 16 + [rng.getrandbits(64) for _ in range(128)]
arg = 0xfedcba98fffffffe
signed = lambda v: v if v < 1 << 63 else v - (1 << 64)
cases = []
for op in ['add','min','max','smin','smax','and','or','xor','exchange','compare_swap','load','store','sparse']:
    sint = op in ['smin','smax']
    ty = 'int64_t' if sint else 'uint64_t'
    vec = 'i64vec4' if sint else 'u64vec4'
    fmt, img = ('r64i','i64image2D') if sint else ('r64ui','u64image2D')
    fn = {'smin':'Min','smax':'Max','compare_swap':'CompSwap'}.get(op,op.title())
    initial = values + [0xdeadc0dedeadc0de] * 1280
    expected = list(initial)
    for i,old in enumerate(values):
        if op == 'add': result = old + arg
        elif op in ['min','max']:result=(min if op=='min' else max)(old,arg)
        elif sint:result=(min if op=='smin' else max)(signed(old),signed(arg))
        elif op == 'and':result=old & arg
        elif op == 'or':result=old | arg
        elif op == 'xor':result=old ^ arg
        elif op in ['exchange','store']:result=arg
        elif op == 'load':result=old
        elif op == 'sparse':result=1
        else:result=arg if i%2==0 else old
        expected[768+i] = old
        expected[1024+i] = result & mask
    for suffix,data in [('input',initial),('expected',expected)]:
        (root/f'{op}-{suffix}.bin').write_bytes(struct.pack('<1536Q',*data))
    operand = 'arg'
    if op=='compare_swap':operand='(i&1u)==0u?imageLoad(tex,p).x:~imageLoad(tex,p).x,arg'
    operation=f'{ty} old=imageAtomic{fn}(tex,p,{operand});'
    if op=='load':operation=f'{ty} old=imageLoad(tex,p).x;'
    if op=='store':operation=f'{ty} old=imageLoad(tex,p).x;imageStore(tex,p,{vec}(arg));'
    if op=='sparse':operation=f'{vec} value;int code=sparseImageLoadARB(tex,p,value);{ty} old=value.x;'
    second = f'{ty}(sparseTexelsResidentARB(code))' if op=='sparse' else 'imageLoad(tex,p).x'
    source=f'''#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_image_int64 : require
#extension GL_ARB_sparse_texture2 : require
layout(local_size_x=64) in;
layout(set=0,binding=0,{fmt}) uniform coherent {img} tex;
layout(set=0,binding=1,std430) buffer Out {{{ty} data[];}} dst;
void main(){{uint i=gl_GlobalInvocationID.x;ivec2 p=ivec2(i%16u,i/16u);
{ty} arg={ty}(0xfedcba98fffffffeul);
{operation}
memoryBarrierImage();dst.data[i]=old;dst.data[256u+i]={second};
}}
'''
    (root/f'{op}.comp').write_text(source)
    subprocess.run(['glslangValidator','-V',str(root/f'{op}.comp'),'-o',str(root/f'{op}.spv')],check=True)
    subprocess.run(['spirv-val','--target-env','vulkan1.1',str(root/f'{op}.spv')],check=True)
    cases.append(dict(name=op,env=dict(M4_ORACLE_BINDING_COUNT='2',M4_ORACLE_IMAGE_FORMAT='111' if sint else '110',M4_ORACLE_STORAGE_IMAGE='1',M4_ORACLE_IMAGE_ATOMIC64='1',M4_ORACLE_RESIDENCY='1' if op=='sparse' else '0')))
for case in cases:
    if case['name'] != 'sparse':case['env'].pop('M4_ORACLE_RESIDENCY')
# A return-value histogram checks all contending updates without depending on
# invocation order or reading a final counter before other workgroups finish.
for count in [64, 256, 1024]:
    name = f'contention-{count}'
    half_words = max(1024, count + 256)
    initial = [0xdeadc0de] * (half_words * 2)
    initial[:2] = [0xffffff80, 0xffffffff]
    initial[half_words:half_words+count] = [0] * count
    expected = list(initial)
    expected[half_words:half_words+count] = [1] * count
    for suffix,data in [('input',initial),('expected',expected)]:
        (root/f'{name}-{suffix}.bin').write_bytes(struct.pack('<'+str(len(data))+'I',*data))
    source = f"""#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_image_int64 : require
layout(local_size_x=64) in;
layout(set=0,binding=0,r64ui) uniform coherent u64image2D tex;
layout(set=0,binding=1,std430) buffer Out {{uint data[];}} dst;
void main(){{
 uint64_t old=imageAtomicAdd(tex,ivec2(0),uint64_t(1));
 uint64_t ordinal=old-uint64_t(0xffffffffffffff80ul);
 if(ordinal<uint64_t({count})) atomicAdd(dst.data[uint(ordinal)],1u);
 else atomicOr(dst.data[{count}],1u);
}}
"""
    (root/f'{name}.comp').write_text(source)
    subprocess.run(['glslangValidator','-V',str(root/f'{name}.comp'),'-o',str(root/f'{name}.spv')],check=True)
    subprocess.run(['spirv-val','--target-env','vulkan1.1',str(root/f'{name}.spv')],check=True)
    cases.append(dict(name=name,groups=count//64,env=dict(M4_ORACLE_BINDING_COUNT='2',M4_ORACLE_IMAGE_FORMAT='110',M4_ORACLE_STORAGE_IMAGE='1',M4_ORACLE_IMAGE_ATOMIC64='1')))
(root/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
(root/'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
for c in json.loads((r/'cases.json').read_text()):
 name=c['name'];print('IMAGE64_CASE:',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(name+'.spv')),str(r/(name+'-input.bin')),str(r/(name+'-expected.bin')),str(c.get('groups',4)),str(out/(name+'.bin'))],env=dict(os.environ,**c['env']),stdout=log,stderr=subprocess.STDOUT,timeout=30)
 print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True)
 if p.returncode:sys.exit(1)
print('PASS: sixteen image64 operation load-store residency and contention configurations',flush=True)
''')
print(root,len(cases))
