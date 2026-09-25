#!/usr/bin/env python3
"""Generate full-resolution R64 image atomic addressing oracles. Usage: NEW_DIRECTORY."""
import json,struct,subprocess,sys
from pathlib import Path
r=Path(sys.argv[1]);r.mkdir(exist_ok=False);cases=[]
for w,h in [(4096,2048),(2880,1620)]:
 for signed in [False,True]:
  name=f'{w}x{h}-'+('smin' if signed else 'umax'); n=w*h; stride=((n*8+16383)//16384)*16384
  initial=bytearray(stride*2);initial[stride:]=b'\xcc'*stride;expected=bytearray(initial)
  for i in range(1024):
   arg=0x8000000100000000+i*0x100001
   struct.pack_into('<Q',expected,stride+i*8,0);struct.pack_into('<Q',expected,stride+(1024+i)*8,arg)
  (r/(name+'-input.bin')).write_bytes(initial);(r/(name+'-expected.bin')).write_bytes(expected)
  ty='int64_t' if signed else 'uint64_t';img='i64image2D' if signed else 'u64image2D';fmt='r64i' if signed else 'r64ui';op='Min' if signed else 'Max'
  src=f'''#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_image_int64 : require
layout(local_size_x=64) in;
layout(set=0,binding=0,{fmt}) uniform coherent {img} tex;
layout(set=0,binding=1,std430) buffer Out {{{ty} data[];}} dst;
void main() {{ uint i=gl_GlobalInvocationID.x;uint q=i==0u?{n-1}u:((i-1u)*8191u)%{n}u;ivec2 p=ivec2(q%{w}u,q/{w}u);
{ty} value={ty}(0x8000000100000000ul+uint64_t(i)*0x100001ul);dst.data[i]=imageAtomic{op}(tex,p,value);memoryBarrierImage();dst.data[1024u+i]=imageLoad(tex,p).x; }}
'''
  (r/(name+'.comp')).write_text(src);subprocess.run(['glslangValidator','-V',str(r/(name+'.comp')),'-o',str(r/(name+'.spv'))],check=True,stdout=subprocess.DEVNULL);subprocess.run(['spirv-val',str(r/(name+'.spv'))],check=True)
  cases.append(dict(name=name,groups=16,env=dict(M4_ORACLE_BINDING_COUNT='2',M4_ORACLE_IMAGE_FORMAT='111' if signed else '110',M4_ORACLE_STORAGE_IMAGE='1',M4_ORACLE_IMAGE_ATOMIC64='1',M4_ORACLE_IMAGE_EXTENT=f'{w}x{h}')))
(r/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
