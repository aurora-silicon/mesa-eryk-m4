#!/usr/bin/env python3
"""Generate exact runtime integer division/remainder and negation regressions.

CPU reference uses modulo-2^32 arithmetic. No GPU runs here. Execute probe.spv
with vk-buffer-oracle, under gpu-experiment.py on M4. Workgroup count is in the
manifest. Includes Blender's divisor151 boundary and full-width edge operands.
"""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
pairs=[(n,151) for n in range(1024)]
edges=[0,1,2,3,6,7,75,76,150,151,152,65535,65536,0x7ffffffe,0x7fffffff,0x80000000,0xfffffffe,0xffffffff]
for d in [1,2,3,7,65535,65537,0x7fffffff,0x80000000,0xffffffff]:
 pairs.extend((n,d) for n in edges)
while len(pairs)%32:pairs.append((0,151))
source='''#version 450
layout(local_size_x=32) in;
layout(set=0,binding=0,std430) buffer B {uint w[];};
void main(){uint b=gl_GlobalInvocationID.x*8u,n=w[b],d=w[b+1];
w[b+2]=n/d;w[b+3]=n%d;w[b+4]=0u-n;w[b+5]=7u-n;
w[b+6]=255u-n;w[b+7]=0u-(n<<2u);}
'''
initial=[];reference=[]
for n,d in pairs:
 initial.extend([n,d]+[0]*6)
 reference.extend([n,d,n//d,n%d,(-n)&0xffffffff,(7-n)&0xffffffff,(255-n)&0xffffffff,(-(n<<2))&0xffffffff])
for name,values in [('input',initial),('reference',reference)]:
 (a.output/(name+'.bin')).write_bytes(struct.pack('<'+'I'*len(values),*values))
(a.output/'probe.comp').write_text(source)
cmd=['glslangValidator','-V',str(a.output/'probe.comp'),'-o',str(a.output/'probe.spv')]
r=subprocess.run(cmd,text=True,capture_output=True);(a.output/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
m=dict(lanes=len(pairs),workgroups=len(pairs)//32,words=len(initial),gpu_executed=False,reference='Python integer quotient/remainder and modulo-2^32 subtraction',command=cmd,hashes={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in a.output.iterdir() if f.is_file()})
(a.output/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(m))
