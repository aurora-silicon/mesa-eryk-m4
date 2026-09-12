#!/usr/bin/env python3
"""Generate a deterministic SSBO atomic regression for vk-buffer-oracle.

Each active lane owns its addresses, so old values and final memory are exact.
Execute hardware only under gpu-experiment.py. Compile probe.comp with glslang.
"""
import argparse,json,struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
initial=[];expected=[]
for lane in range(64):
 values=[100+lane]*8;result=[0]*8
 if lane&1:
  result=values[:]
  values=[values[0]+3,5,7 if lane&2 else values[2],50,200,values[5]&15,values[6]|256,values[7]^85]
 initial+= [100+lane]*8+[0]*8
 expected+=values+result
source='''#version 450
layout(local_size_x=64) in;
layout(std430,set=0,binding=0) buffer B { uint w[]; };
void main(){
 uint i=gl_GlobalInvocationID.x, b=i*16u;
 if((i&1u)==0u)return;
 w[b+8]=atomicAdd(w[b],3u);
 w[b+9]=atomicExchange(w[b+1],5u);
 w[b+10]=atomicCompSwap(w[b+2],(i&2u)!=0u?100u+i:999u,7u);
 w[b+11]=atomicMin(w[b+3],50u);
 w[b+12]=atomicMax(w[b+4],200u);
 w[b+13]=atomicAnd(w[b+5],15u);
 w[b+14]=atomicOr(w[b+6],256u);
 w[b+15]=atomicXor(w[b+7],85u);
}
'''
(a.output/'probe.comp').write_text(source)
for name,words in [('input',initial),('reference',expected)]:
 (a.output/(name+'.bin')).write_bytes(struct.pack('<%dI'%len(words),*words))
(a.output/'manifest.json').write_text(json.dumps(dict(lanes=64,groups=1,operations=['add','exchange','compare-exchange','unsigned min','unsigned max','and','or','xor'],oracle='integer CPU semantics, unique addresses per lane'),indent=2)+'\n')
