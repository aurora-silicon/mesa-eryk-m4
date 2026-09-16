#!/usr/bin/env python3
"""Generate mixed ordinary/atomic alias and reversed sparse-page oracles.

Usage: buffer-alias-cases.py NEW_DIRECTORY
Run generated run.py OUTPUT ORACLE only inside the mandatory hardware guard.
"""
import json
from pathlib import Path
import struct
import subprocess
import sys
root=Path(sys.argv[1]);root.mkdir(parents=True,exist_ok=False)
cases=[]
for bits in [32,64]:
 for reverse in [False,True]:
  for op in ['load','store_load','atomic_load','store_atomic']:
   name=f'{bits}-{int(reverse)}-{op}';size=131072;step=bits//8
   initial=bytearray(size)
   for i in range(size//step):struct.pack_into('<I' if bits==32 else '<Q',initial,i*step,0x10203040+i)
   expected=bytearray(initial)
   for i in range(64):
    for j in range(4):
     p=(16384//step-1)+i*4+j
     byte=p*step
     physical=((size//16384-1-byte//16384)*16384+byte%16384) if reverse else byte
     old=struct.unpack_from('<I' if bits==32 else '<Q',expected,physical)[0]
     value=old if op=='load' else 0x31415900+i*4+j
     if op!='load':struct.pack_into('<I' if bits==32 else '<Q',expected,physical,value)
     struct.pack_into('<I' if bits==32 else '<Q',expected,(i*4+j)*step,value)
   for suffix,data in [('input',initial),('expected',expected)]: (root/f'{name}-{suffix}.bin').write_bytes(data)
   ty='uint' if bits==32 else 'uint64_t'
   write={'load':'','store_load':'alias.data[p]=v;','atomic_load':'atomicExchange(alias.data[p],v);','store_atomic':'alias.data[p]=v;'}[op]
   read='atomicOr(alias.data[p],'+ty+'(0))' if op=='store_atomic' else 'alias.data[p]'
   src=f'''#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_atomic_int64 : require
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) coherent buffer Alias {{{ty} data[];}} alias;
layout(set=0,binding=1,std430) coherent buffer Original {{{ty} data[];}} original;
void main(){{uint i=gl_GlobalInvocationID.x;for(uint j=0u;j<4u;j++){{uint p={16384//step-1}u+i*4u+j;{ty} v={ty}(0x31415900u+i*4u+j);{write}memoryBarrierBuffer();original.data[i*4u+j]={read};}}}}
'''
   (root/f'{name}.comp').write_text(src)
   subprocess.run(['glslangValidator','-V',str(root/f'{name}.comp'),'-o',str(root/f'{name}.spv')],check=True)
   subprocess.run(['spirv-val','--target-env','vulkan1.1',str(root/f'{name}.spv')],check=True)
   env=dict(M4_ORACLE_BINDING_COUNT='2',M4_ORACLE_SPARSE_ALIAS='1',M4_ORACLE_ALIAS_FULL_VIEWS='1')
   if reverse:env['M4_ORACLE_ALIAS_REVERSE_PAGES']='1'
   if bits==64:env['M4_ORACLE_ATOMIC64']='1'
   cases.append(dict(name=name,groups=1,env=env))
(root/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
(root/'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
failed=0
for c in json.loads((r/'cases.json').read_text()):
 name=c['name'];print('ALIAS_CASE:',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(name+'.spv')),str(r/(name+'-input.bin')),str(r/(name+'-expected.bin')),str(c['groups']),str(out/(name+'.bin'))],env=dict(os.environ,**c['env']),stdout=log,stderr=subprocess.STDOUT,timeout=20)
 print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True);failed+=p.returncode!=0
 if p.returncode:break
print(('FAIL' if failed else 'PASS')+': sixteen mixed sparse alias configurations',flush=True)
sys.exit(bool(failed))
''')
print(root,len(cases))
