#!/usr/bin/env python3
"""Generate eight signed/unsigned formatless R64 image and texel-buffer probes.

Run generated run.py OUTPUT ORACLE only under the mandatory hardware guard.
"""
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

root=Path(sys.argv[1]);root.mkdir(parents=True,exist_ok=False)
rng=random.Random(0x64f04)
mask=(1<<64)-1
edge=[0,mask,1<<63,(1<<63)-1,0xffffffff,0x100000000,0x123456789abcdef,0xfedcba9876543210]
values=edge*16+[rng.getrandbits(64) for _ in range(128)]
arg=0xfedcba98fffffffe
cases=[]
for buffer in [False,True]:
 for signed in [False,True]:
  for store in [False,True]:
   name=('buffer' if buffer else 'image')+('-signed' if signed else '-unsigned')+('-store' if store else '-load')
   ty,vec=('int64_t','i64vec4') if signed else ('uint64_t','u64vec4')
   fmt='r64i' if signed else 'r64ui'
   image=('i64' if signed else 'u64')+('imageBuffer' if buffer else 'image2D')
   qualifier='writeonly' if store else 'readonly'
   coord='int p=int(i);' if buffer else 'ivec2 p=ivec2(i%16u,i/16u);'
   declaration=f'layout(set=0,binding=0) uniform coherent {qualifier} {image} tex;'
   body=f'{ty} old=imageLoad(tex,p).x;'
   result='imageLoad(tex,p).x'
   if store:
    declaration+=f'\nlayout(set=0,binding=0,{fmt}) uniform coherent readonly {image} readback;'
    body=f'{ty} old=imageLoad(readback,p).x;imageStore(tex,p,{vec}({ty}(0xfedcba98fffffffeul)));'
    result='imageLoad(readback,p).x'
   source=f'''#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_image_int64 : require
#extension GL_EXT_shader_image_load_formatted : require
layout(local_size_x=64) in;
{declaration}
layout(set=0,binding=1,std430) buffer Out {{{ty} data[];}} dst;
void main(){{uint i=gl_GlobalInvocationID.x;{coord}
{body}
memoryBarrierImage();dst.data[i]=old;dst.data[256u+i]={result};
}}
'''
   initial=values+[0xdeadc0dedeadc0de]*1280
   expected=list(initial)
   expected[768:1024]=values
   expected[1024:1280]=[arg]*256 if store else values
   if store and buffer:expected[:256]=[arg]*256
   for suffix,data in [('input',initial),('expected',expected)]:
    (root/f'{name}-{suffix}.bin').write_bytes(struct.pack('<1536Q',*data))
   (root/f'{name}.comp').write_text(source)
   subprocess.run(['glslangValidator','-V',str(root/f'{name}.comp'),'-o',str(root/f'{name}.spv')],check=True)
   subprocess.run(['spirv-val','--target-env','vulkan1.1',str(root/f'{name}.spv')],check=True)
   env=dict(M4_ORACLE_BINDING_COUNT='2',M4_ORACLE_IMAGE_ATOMIC64='1',M4_ORACLE_IMAGE_FORMATLESS='1')
   if buffer:env.update(M4_ORACLE_TEXEL_FORMAT='111' if signed else '110',M4_ORACLE_TEXEL_STORAGE='1',M4_ORACLE_TEXEL_OFFSET='0',M4_ORACLE_TEXEL_RANGE='2048')
   else:env.update(M4_ORACLE_IMAGE_FORMAT='111' if signed else '110',M4_ORACLE_STORAGE_IMAGE='1')
   cases.append(dict(name=name,env=env))
(root/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
(root/'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
failed=0
for c in json.loads((r/'cases.json').read_text()):
 name=c['name'];print('FORMATLESS64_CASE:',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(name+'.spv')),str(r/(name+'-input.bin')),str(r/(name+'-expected.bin')),'4',str(out/(name+'.bin'))],env=dict(os.environ,**c['env']),stdout=log,stderr=subprocess.STDOUT,timeout=30)
 print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True);failed+=p.returncode!=0
print(('FAIL' if failed else 'PASS')+': eight formatless integer64 image and buffer configurations',flush=True)
sys.exit(bool(failed))
''')
print(root,len(cases))
