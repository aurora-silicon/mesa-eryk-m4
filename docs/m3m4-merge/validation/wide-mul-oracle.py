#!/usr/bin/env python3
"""Generate exact integer-wide products for guarded vk-buffer-oracle."""
import argparse, json, random, struct, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
source='''#version 450
#extension GL_ARB_gpu_shader_int64 : require
layout(local_size_x=32) in;
layout(set=0,binding=0,std430) buffer B {uint w[];};
void main(){uint p=gl_GlobalInvocationID.x*16u;
uint x=w[p],y=w[p+1u],z=w[p+2u],t=w[p+3u],lo,hi;
int slo,shi;umulExtended(x,y,hi,lo);w[p+4u]=lo;w[p+5u]=hi;
imulExtended(int(x),int(y),shi,slo);w[p+6u]=uint(slo);w[p+7u]=uint(shi);
uint64_t u=uint64_t(x)*uint64_t(y);w[p+8u]=uint(u);w[p+9u]=uint(u>>32);
int64_t s=int64_t(int(x))*int64_t(int(y));w[p+10u]=uint(s);w[p+11u]=uint(s>>32);
uint64_t v=(uint64_t(x)|(uint64_t(z)<<32))*(uint64_t(y)|(uint64_t(t)<<32));w[p+12u]=uint(v);w[p+13u]=uint(v>>32);
}
'''
(a.output/'probe.comp').write_text(source)
subprocess.run(['glslangValidator','-V',str(a.output/'probe.comp'),'-o',str(a.output/'probe.spv')],check=True)
subprocess.run(['spirv-val','--target-env','vulkan1.1',str(a.output/'probe.spv')],check=True)
r=random.Random(0x3264); initial=bytearray();expected=bytearray()
edge=[0,1,2,3,0x7fff,0x8000,0xffff,0x10000,0x7fffffff,0x80000000,0x80000001,0xfffffffe,0xffffffff]
def signed(x):return x if x<0x80000000 else x-(1<<32)
def words(x):return [x&0xffffffff,(x>>32)&0xffffffff]
for i in range(65536):
 x,y= (edge[i//len(edge)],edge[i%len(edge)]) if i<len(edge)**2 else (r.getrandbits(32),r.getrandbits(32))
 z,t=r.getrandbits(32),r.getrandbits(32);hdr=[x,y,z,t]
 vals=words(x*y)+words(signed(x)*signed(y))+words(x*y)+words(signed(x)*signed(y))+words((x|(z<<32))*(y|(t<<32)))
 initial+=struct.pack('<16I',*(hdr+[0xdeadbeef]*12));expected+=struct.pack('<16I',*(hdr+vals+[0xdeadbeef]*2))
(a.output/'input.bin').write_bytes(initial);(a.output/'expected.bin').write_bytes(expected)
(a.output/'manifest.json').write_text(json.dumps(dict(groups=2048,lanes=65536,results=655360,reference='Python integer multiply; signed and modulo64'),indent=2)+'\n')
