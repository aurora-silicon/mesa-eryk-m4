#!/usr/bin/env python3
"""Generate constant-mip oracles for sampler bias/clamp plus image-view MinLod.

Usage: view-min-lod-cases.py NEW_DIRECTORY
Use vk-buffer-oracle.c and run generated run.py under gpu-experiment.py.
"""
from pathlib import Path
import json
import struct
import subprocess
import sys

root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=False)
initial = bytearray(32768); offset = 0
for level in range(5):
    for _ in range((16 >> level) ** 2):
        struct.pack_into('<f', initial, offset, level * 16.); offset += 4
(root / 'input.bin').write_bytes(initial)
cases = []
for lod, bias, maximum, view in [(.5, 0, 4, 0), (.5, 0, 4, 1.25),
        (.5, 1, 4, 1.25), (2, -1, 4, 1.25), (2, 0, .5, 1.25),
        (0, 1, 4, 2), (4, -1, 4, 2), (2, 0, 4, 2)]:
    name = f'{len(cases):02d}'
    expected = bytearray(initial)
    value = max(view, max(0, min(maximum, lod + bias))) * 16
    for i in range(256): struct.pack_into('<4f', expected, 16384 + i * 16, *([value] * 4))
    (root / (name + '.bin')).write_bytes(expected)
    shader = f'''#version 450
layout(local_size_x=64) in;
layout(set=0,binding=0) uniform sampler2D tex;
layout(set=0,binding=1,std430) writeonly buffer Out {{vec4 data[];}} dst;
void main() {{ uint i=gl_GlobalInvocationID.x; vec2 uv=(vec2(i%16u,i/16u)+.25)/16.;
 vec2 dx=vec2({2**lod/16},0),dy=dx.yx;
 dst.data[i]=vec4(textureLod(tex,uv,{float(lod)}).r,textureGrad(tex,uv,dx,dy).r,
 textureLodOffset(tex,uv,{float(lod)},ivec2(1,-1)).r,
 textureGradOffset(tex,uv,dx,dy,ivec2(1,-1)).r); }}
'''
    (root / (name + '.comp')).write_text(shader)
    subprocess.run(['glslangValidator','-V',str(root/(name+'.comp')),'-o',str(root/(name+'.spv'))],check=True,stdout=subprocess.DEVNULL)
    subprocess.run(['spirv-val','--target-env','vulkan1.1',str(root/(name+'.spv'))],check=True)
    env = dict(M4_ORACLE_BINDING_COUNT='2', M4_ORACLE_IMAGE_FORMAT='100',
        M4_ORACLE_IMAGE_MIPS='5', M4_ORACLE_LINEAR='1', M4_ORACLE_MIP_LINEAR='1',
        M4_ORACLE_VIEW_MIN_LOD=str(view), M4_ORACLE_SAMPLER_BIAS=str(bias),
        M4_ORACLE_SAMPLER_MAX=str(maximum))
    cases.append(dict(name=name,lod=lod,bias=bias,maximum=maximum,view=view,expected=value,env=env))
(root / 'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
(root / 'run.py').write_text('''#!/usr/bin/env python3
import json,math,os,struct,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
failed=0
for c in json.loads((r/'cases.json').read_text()):
 n=c['name'];print('VIEW_MIN_CASE:',n,flush=True)
 with (out/(n+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(n+'.spv')),str(r/'input.bin'),str(r/(n+'.bin')),'4',str(out/(n+'.bin'))],env=dict(os.environ,**c['env']),timeout=30,stdout=log,stderr=subprocess.STDOUT)
 actual=(out/(n+'.bin')).read_bytes() if (out/(n+'.bin')).exists() else b''
 expected=(r/(n+'.bin')).read_bytes()
 good=p.returncode in (0,1) and len(actual)==len(expected) and actual[:16384]==expected[:16384] and actual[20480:]==expected[20480:]
 worst=0.
 if good:
  for offset in range(16384,20480,4):
   a=struct.unpack_from('<f',actual,offset)[0];e=struct.unpack_from('<f',expected,offset)[0]
   error=abs(a-e) if math.isfinite(a) else float('inf')
   worst=max(worst,error)
  # 2/256 LOD at 16 units per level; covers derivative/LOD precision only.
  good=worst<=.125
 print(('PASS' if good else 'FAIL')+': view minimum numeric check '+n+' max_error='+str(worst),flush=True)
 failed+=not good
print(('FAIL' if failed else 'PASS')+': view minimum sampler combinations',flush=True)
sys.exit(bool(failed))
''')
print(f'{len(cases)} view minimum sampler combinations generated')
