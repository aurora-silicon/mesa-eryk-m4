#!/usr/bin/env python3
"""Make a numerical culling probe from vkQuake's saved compute resource dump.

Diagnostic adaptation: concatenate captured SSBOs into one buffer, specialize
push constants, and record each invocation's visibility/backface decision.
Run with vk-buffer-oracle; hardware execution requires gpu-experiment.py.
"""
import argparse, json, struct, hashlib, re
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--dump',type=Path,required=True)
p.add_argument('--push-constants',type=Path,required=True)
p.add_argument('--globals',type=Path,required=True)
p.add_argument('--dispatch',type=int,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--mode', choices=['decisions','generate'], default='decisions')
a=p.parse_args(); out=a.output;out.mkdir(parents=True,exist_ok=False)
commands=[d for x in json.loads(a.dump.read_text()) for d in x.get('dispatchCommands',[]) if d['dispatchIndex']==a.dispatch]
assert len(commands)==1
entries={d['binding']:d for d in commands[0]['descriptors']['compute']}
values=struct.unpack('<IIIfffI',a.push_constants.read_bytes())
num,first,vis,vx,vy,vz,base=values
assert first==0 and 0<num<1000000
# Fixed arrays preserve the source's std430 struct strides.
names=['indirect_draw_data','surfaces','visibility','indices','surface_submodels','bmodel_instances']
types=['draw_indirect_command_t','surface_t','uint','uint','uint','bmodel_instance_t']
strides=[20,64,4,4,4,64]
blob=bytearray();fields=[];hashes={}
for b in range(6):
 path=a.dump.parent/Path(entries[b]['beforeFile']).name
 data=path.read_bytes();assert len(data)%strides[b]==0
 blob.extend(bytes((-len(blob))%16))
 fields.append(f'layout(offset={len(blob)}) {types[b]} {names[b]}[{len(data)//strides[b]}];')
 blob.extend(data);hashes[str(path)]=hashlib.sha256(data).hexdigest()
blob.extend(bytes((-len(blob))%16));offset=len(blob)
fields.append(f'layout(offset={offset}) uvec4 probe[];')
blob.extend(bytes(num*32))
shader='''#version 450
layout(local_size_x=64) in;
'''+a.globals.read_text()+'\nlayout(std430,set=0,binding=0) buffer Data {\n'+'\n'.join(fields)+'\n};\n'+f'''
void main() {{
 uint surf=gl_GlobalInvocationID.x;
 if(surf>={num}u) return;
 uint word=visibility[{vis}u+surf/32u];
 uint mask=1u<<(surf%32u);
 uint visible=uint((word&mask)!=0u);
 uint sub=surface_submodels[surf]&0x7fffffffu;
 vec3 vieworg=sub!=0u ? bmodel_instances[{base}u+sub].local_vieworg.xyz : vec3({vx!r},{vy!r},{vz!r});
 vec3 normal=vec3(surfaces[surf].normal_x,surfaces[surf].normal_y,surfaces[surf].normal_z);
 float dp=dot(normal,vieworg), dist=surfaces[surf].dist;
 bool back=(surfaces[surf].packed_tex_edgecount&0x8000u)!=0u;
 uint culled=uint((back&&dp>dist)||(!back&&dp<dist));
 probe[surf*2u]=uvec4(word,mask,visible,culled);
 probe[surf*2u+1u]=uvec4(floatBitsToUint(dp),floatBitsToUint(dist),surfaces[surf].packed_tex_edgecount,sub);
}}
'''
(out/'probe.comp').write_text(shader);(out/'input.bin').write_bytes(blob)
if a.mode == 'generate':
    source = a.globals.with_name('indirect.comp').read_text()
    functions = source[source.index('uint R_NumTriangleIndicesForSurf'):]
    functions = functions.replace('push_constants.', 'pc.')
    functions = functions.replace('const uint vis_word',
        'probe[surf*2u].x=1u;\n const uint vis_word')
    functions = functions.replace('// surface planes',
        'probe[surf*2u].y=1u;\n // surface planes')
    functions = functions.replace('uint\t   tex_idx',
        'probe[surf*2u].z=1u;\n uint tex_idx')
    functions = functions.replace('R_TriangleIndicesForSurf (firstvert, numedges, dest);',
        'probe[surf*2u].w=1u;\n probe[surf*2u+1u]=uvec4(tex_idx,indexcount,dest,firstvert);\n R_TriangleIndicesForSurf (firstvert, numedges, dest);')
    constants = f'''struct Push {{ uint num_draws; uint first_draw; uint visibility_offset;
float vieworg_x; float vieworg_y; float vieworg_z; uint instance_base; }};
const Push pc=Push({num}u,{first}u,{vis}u,{vx!r},{vy!r},{vz!r},{base}u);
'''
    shader = shader[:shader.index('void main()')] + constants + functions
    (out/'probe.comp').write_text(shader)
(out/'manifest.json').write_text(json.dumps(dict(mode=a.mode,dispatch=a.dispatch,push_constants=values,output_offset=offset,invocations=num,groups=(num+63)//64,hashes=hashes,adaptations=['single combined SSBO','constant push values','per-invocation decision output']),indent=2)+'\n')
print(out)
