#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate MSL parallax-loop witnesses for m4-metal-cross-compile.py."""
from pathlib import Path
import argparse
import itertools
import json

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=Path)
args = parser.parse_args()
out = args.output
out.mkdir(parents=True, exist_ok=True)
header='''#include <metal_stdlib>
using namespace metal;
struct V {float4 pos [[position]];float2 uv;float3 eye;};
vertex V vs(uint i [[vertex_id]]) {float2 p=float2((i<<1)&2,i&2);return {float4(p*2-1,0,1),p,float3(p*200.0-100.0,10.0)};}
'''
body='''
fragment float4 fs(V v [[stage_in]],texture2d<float> normTex [[texture(0)]],texture2d<float> baseTex [[texture(1)]],depth2d<float> shadowTex [[texture(2)]],sampler s [[sampler(0)]],sampler s2 [[sampler(1)]],sampler ss [[sampler(2)]],constant float *u [[buffer(1)]]) {
 float2 coord=v.uv;
 float h=normTex.sample(s,coord).a;
 float3 eyeDir=normalize(v.eye);
 float dist=length(v.eye);
 int iter=BOUND;
 float height=1.0, step=1.0/float(iter), t=step;
 float2 move=float2(-eyeDir.x,eyeDir.y)*u[1]/(eyeDir.z*float(iter));
 if(iter>0){
  for(int i=0;i<iter;i++) {
   height-=t;t=h<height?step:0.0;coord+=h<height?move:float2(0);
   h=NORMAL_SAMPLE;
   INNER
  }
  float hp=normTex.sample(s,coord-move).a;
  coord-=move*((h-height)/(step+h-hp));
 }
 float4 color=baseTex.sample(s,coord);
 SHADOW
 return color;
}
'''
manifest=[]
for bound,inner,lod,shadow in itertools.product(('uniform','varying'),(False,True),('auto','zero'),(False,True)):
 name=f'loop-{bound}-if{int(inner)}-{lod}-shadow{int(shadow)}'
 src=body.replace('BOUND','int(min(20.0,u[0]))' if bound=='uniform' else 'int(min(20.0,25.0-dist/10.0))')
 sample='normTex.sample(s,coord'+(',level(0.0)' if lod=='zero' else '')+').a'
 other='normTex.sample(s2,coord'+(',level(0.0)' if lod=='zero' else '')+').a'
 src=src.replace('NORMAL_SAMPLE',sample).replace('INNER',f'if(u[3]>0.0){{float h2={other};h=mix(h2,h,u[4]);}}' if inner else '')
 src=src.replace('SHADOW','float z=u[2];color.rgb*=0.25*(shadowTex.sample_compare(ss,v.uv,z)+shadowTex.sample_compare(ss,v.uv+float2(.001,0),z)+shadowTex.sample_compare(ss,v.uv+float2(0,.001),z)+shadowTex.sample_compare(ss,v.uv+float2(.001),z));' if shadow else '')
 (out/(name+'.metal')).write_text(header+src)
 manifest.append(dict(name=name,bound=bound,inner=inner,lod=lod,shadow=shadow))
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))

# Remove every implicit sample as a separate control, including those before
# and after the loop. The zero variants above only change the loop samples.
for inner in (0, 1):
 name = f'loop-varying-if{inner}-allzero-shadow0'
 source = (out / f'loop-varying-if{inner}-zero-shadow0.metal').read_text()
 source = source.replace('normTex.sample(s,coord)', 'normTex.sample(s,coord,level(0.0))')
 source = source.replace('normTex.sample(s,coord-move)', 'normTex.sample(s,coord-move,level(0.0))')
 source = source.replace('baseTex.sample(s,coord)', 'baseTex.sample(s,coord,level(0.0))')
 (out / (name + '.metal')).write_text(source)
 manifest.append(dict(name=name,bound='varying',inner=bool(inner),lod='allzero',shadow=False))
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
