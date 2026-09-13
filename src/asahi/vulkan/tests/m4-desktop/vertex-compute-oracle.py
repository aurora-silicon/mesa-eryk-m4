#!/usr/bin/env python3
"""Convert a captured, attribute-free GLSL vertex calculation into an SSBO oracle.

Accepts SPIRV-Cross Vulkan GLSL with std140 UBOs and a scalar push block.
Preserves shader calculations, packs captured inputs in one std140 storage
buffer, and exports positions plus requested vec4 expressions per vertex.
This diagnostic changes shader stage; it does not qualify the original draw.
Run generated SPIR-V through vk-buffer-oracle, guarded on M4.
"""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path)
p.add_argument('--draw-dump',required=True,type=Path)
p.add_argument('--push-constants',required=True,type=Path)
p.add_argument('--vertices',required=True,type=int)
p.add_argument('--first-vertex',type=int,default=0)
p.add_argument('--value',action='append',default=[],help='Additional vec4 output expression')
p.add_argument('--output',required=True,type=Path)
a=p.parse_args()
if not 1<=a.vertices<=65536:p.error('Require 1..65536 vertices')
s=a.source.read_text()
if re.search(r'layout\s*\([^)]*location[^)]*\)\s*(?:(?:flat|noperspective)\s+)?in\b',s):
 p.error('Vertex attributes require an explicit adapter')
if any(x in s for x in ('gl_InstanceIndex','sampler','buffer ')):
 p.error('Instance/texture/storage inputs require an explicit adapter')
rows=json.loads(a.draw_dump.read_text());row=next(r for r in rows if 'drawIndex' in r)
desc={d['binding']:d for d in row['descriptors']['vertex'] if d['set']==0 and d['arrayIndex']==0 and d['type']=='VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER'}
initial=bytearray();members=[];files=[a.source,a.draw_dump,a.push_constants];blocks=[]
def block(m):
 layout,name,body,instance=m.groups()
 if 'push_constant' in layout:
  # This adapter supports scalar push members only, for std430/std140 equivalence.
  if not all(re.fullmatch(r'\s*(?:int|uint|float)\s+\w+\s*',x) for x in body.split(';') if x.strip()):
   raise ValueError('Only scalar push-block members are qualified')
  f=a.push_constants
 else:
  if 'std140' not in layout or not re.search(r'set\s*=\s*0\b',layout):raise ValueError('Only set0 std140 UBOs are supported')
  binding=int(re.search(r'binding\s*=\s*(\d+)',layout)[1]);d=desc[binding]
  f=a.draw_dump.parent/Path(d['file']).name
  if f.stat().st_size!=d['size']:raise ValueError('Captured descriptor size mismatch')
 files.append(f);initial.extend(bytes((-len(initial))%16));offset=len(initial);initial.extend(f.read_bytes())
 members.append(f'layout(offset={offset}) {name} {instance};')
 blocks.append(dict(type=name,instance=instance,offset=offset,size=f.stat().st_size,source=str(f)))
 return f'struct {name}\n{{{body}}};'
s=re.sub(r'layout\(([^)]*)\)\s+uniform\s+(\w+)\s*\{([^{}]*)\}\s*(\w+)\s*;',block,s)
if not blocks:raise ValueError('No uniform blocks found')
s=re.sub(r'layout\(location[^)]*\)\s*(?:(?:flat|noperspective)\s+)?out\s+', '',s)
s=s.replace('void main()', 'void original_vertex()').replace('gl_VertexIndex',f'(int(gl_GlobalInvocationID.x)+{a.first_vertex})')
initial.extend(bytes((-len(initial))%16));offset=len(initial);stride=1+len(a.value)
members.append(f'layout(offset={offset}) vec4 results[];')
extra='layout(local_size_x=32) in;\nvec4 gl_Position;\nlayout(set=0,binding=0,std140) buffer Oracle {\n'+'\n'.join(members)+'\n};\n'
s=s.replace('void original_vertex()',extra+'\nvoid original_vertex()')
s+='\nvoid main(){uint i=gl_GlobalInvocationID.x;if(i>='+str(a.vertices)+'u)return;original_vertex();\n'
s+=f'results[i*{stride}u]=mix(gl_Position,vec4(uintBitsToFloat(0x7fc00000u)),isnan(gl_Position));\n'
for i,v in enumerate(a.value,1):s+=f'results[i*{stride}u+{i}u]=any(isnan(gl_Position))?vec4(0):({v});\n'
s+='}\n';s=s.replace('gl_Position','oracle_position');initial.extend(bytes(a.vertices*stride*16))
a.output.mkdir(parents=True,exist_ok=False)
(a.output/'probe.comp').write_text(s);(a.output/'input.bin').write_bytes(initial)
cmd=['glslangValidator','-V',str(a.output/'probe.comp'),'-o',str(a.output/'probe.spv')]
r=subprocess.run(cmd,text=True,capture_output=True);(a.output/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
m=dict(adaptation='vertex calculation as compute, captured UBOs/push constants repacked into one std140 SSBO',draw=row['drawIndex'],vertices=a.vertices,first_vertex=a.first_vertex,output_offset=offset,vec4_stride=stride,values=['gl_Position']+a.value,workgroups=(a.vertices+31)//32,blocks=blocks,command=cmd,gpu_executed=False,hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files})
(a.output/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(dict(output=str(a.output),bytes=len(initial),output_offset=offset,workgroups=m['workgroups'])))
