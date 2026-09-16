#!/usr/bin/env python3
"""Generate exact R32 min/max sampling oracles; run run.py only under the guard.

Usage: minmax-sampler-cases.py NEW_DIRECTORY
The matching vk-buffer-oracle accepts REDUCTION, IMAGE_MIPS, LINEAR, MIP_LINEAR,
ADDRESS, BORDER, SAMPLER_{MIN,MAX,BIAS}, VIEW_MIN_LOD and IMAGE_LAYERS options,
all prefixed M4_ORACLE_. The reference excludes every zero-weight texel.
"""
from pathlib import Path
import json
import math
import struct
import subprocess
import sys

root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=False)
sparse = '--sparse' in sys.argv[2:]
shadow = '--shadow' in sys.argv[2:]
weighted = '--weighted' in sys.argv[2:] or shadow
cases = []
for mode in ([0] if weighted else [1, 2]):
    for address in [0, 1, 2, 3, 4]:
        cases.append(dict(mode=mode, address=address, lod=1.5, miplinear=True))
    for lod in [0, .5, 1, 2.5, 4]:
        cases.append(dict(mode=mode, address=2, lod=lod, miplinear=True))
    for rate in [2, 4, 8, 16]:
        cases.append(dict(mode=mode, address=0, lod=.5, miplinear=True, aniso=rate))
        cases.append(dict(mode=mode, address=2, lod=0, miplinear=True, aniso=rate, aniso_y=True, viewmin=1.25))
    cases.extend([
        dict(mode=mode, address=2, lod=0, miplinear=True, minimum=.01, mag_linear=False),
        dict(mode=mode, address=2, lod=0, miplinear=True, mag_linear=False),
        dict(mode=mode, address=2, lod=0, miplinear=True, minimum=1.01),
        dict(mode=mode, address=2, lod=2, miplinear=True, maximum=1.01),
        dict(mode=mode, address=2, lod=1.5, miplinear=True, linear=False),
        dict(mode=mode, address=2, lod=1.25, miplinear=False),
        dict(mode=mode, address=2, lod=.5, miplinear=True, minimum=1.25),
        dict(mode=mode, address=2, lod=3, miplinear=True, maximum=1.5),
        dict(mode=mode, address=2, lod=.5, miplinear=True, bias=1),
        dict(mode=mode, address=2, lod=.5, miplinear=True, viewmin=1.25),
        dict(mode=mode, address=2, lod=.5, miplinear=True, shader_min=1.25),
        dict(mode=mode, address=2, lod=.5, miplinear=True, layers=2, viewmin=1.25),
        dict(mode=mode, address=2, lod=.5, miplinear=True, bias=1, viewmin=1.25),
        dict(mode=mode, address=2, lod=2, miplinear=True, maximum=.5, viewmin=1.25),
    ])

if shadow: cases = [c for c in cases if c.get('layers',1)==1]
if weighted:
    for c in cases: c.setdefault('viewmin', 1.25)

def texel(level, layer, x, y):
    value = float(((x * 13 + y * 7 + level * 19 + layer * 23) % 61) - 30)
    return float(value > 0) if shadow else value

def addressed(v, size, mode):
    if mode == 0: return v % size
    if mode == 1:
        v %= 2 * size
        return v if v < size else 2 * size - 1 - v
    if mode == 4: v = v if v >= 0 else -v - 1
    if mode in [2, 4]: return max(0, min(size - 1, v))
    return v if 0 <= v < size else None

def sample(c, uv, layer, offset, grad=False):
    if grad and c.get('aniso'):
        isotropic = dict(c); rate = isotropic.pop('aniso')
        values = []
        for tap in range(rate):
            delta = (2 ** c['lod'] * rate / 16) * ((tap + .5) / rate - .5)
            shifted = (uv[0], uv[1] + delta) if c.get('aniso_y') else (uv[0] + delta, uv[1])
            values.append(sample(isotropic, shifted, layer, offset, True))
        return (sum(values) / len(values) if c['mode'] == 0 else (min if c['mode'] == 1 else max)(values))
    lod = c['lod'] + c.get('bias', 0)
    lod = min(c.get('maximum', 4), max(c.get('minimum', 0), lod))
    lod = max(lod, c.get('shader_min', 0) if grad else 0)
    linear = c.get('linear', True) and (c.get('mag_linear', True) if lod <= 0 else True)
    lod = max(lod, c.get('viewmin', 0))
    lod = max(0, min(4, lod))
    levels = [math.floor(lod)] if c['miplinear'] else [math.floor(lod + .5)]
    if c['miplinear'] and lod % 1: levels.append(min(4, levels[0] + 1))
    values = []
    weights = []
    for level in levels:
        size = 16 >> level
        pos = [round((v * size - (.5 if linear else 0)) * 256) / 256 for v in uv]
        base = [math.floor(p) + o for p, o in zip(pos, offset)]
        for y in range(2 if linear and pos[1] % 1 else 1):
            for x in range(2 if linear and pos[0] % 1 else 1):
                px = addressed(base[0] + x, size, c['address'])
                py = addressed(base[1] + y, size, c['address'])
                values.append(1. if px is None or py is None else texel(level, layer, px, py))
                weight = ((pos[0] % 1 if x else 1 - pos[0] % 1) * (pos[1] % 1 if y else 1 - pos[1] % 1)) if linear else 1
                if len(levels) == 2: weight *= (1 - lod % 1 if level == levels[0] else lod % 1)
                weights.append(weight)
    return sum(v*w for v,w in zip(values,weights)) if c['mode'] == 0 else (min if c['mode'] == 1 else max)(values)

for idx, c in enumerate(cases):
    name = f'{idx:02d}-' + ('weighted' if c['mode'] == 0 else 'min' if c['mode'] == 1 else 'max')
    layers = c.get('layers', 1)
    initial = bytearray(32768); off = 0
    for level in range(5):
        size = 16 >> level
        for layer in range(layers):
            for y in range(size):
                for x in range(size):
                    value=texel(level,layer,x,y)
                    struct.pack_into('<f', initial, off, (.75 if value else .25) if shadow else value); off += 4
    expected = bytearray(initial)
    for i in range(256):
        # Include exact texel centres, zero weights, interior and border taps.
        uv = ((i % 16 - 2 + (.5 if i % 3 == 0 else .25)) / 16,
              (i // 16 - 1 + (.5 if i % 3 == 0 else .25)) / 16)
        layer = i % layers
        vals = [sample(c, uv, layer, offset, grad) for offset, grad in
                [((0, 0), False), ((0, 0), True), ((1, -1), False), ((1, -1), True)]]
        struct.pack_into('<4f', expected, 16384 + i * 16, *vals)
    (root / (name + '-input.bin')).write_bytes(initial)
    (root / (name + '.bin')).write_bytes(expected)
    env = dict(M4_ORACLE_BINDING_COUNT='2', M4_ORACLE_IMAGE_FORMAT='100',
               M4_ORACLE_IMAGE_MIPS='5', M4_ORACLE_REDUCTION=str(c['mode']),
               M4_ORACLE_ADDRESS=str(c['address']), M4_ORACLE_BORDER='4')
    if shadow: env.update(M4_ORACLE_IMAGE_FORMAT='126',M4_ORACLE_IMAGE_DEPTH='1')
    if sparse: env['M4_ORACLE_RESIDENCY']='1'
    if c.get('aniso'): env['M4_ORACLE_ANISOTROPY'] = str(c['aniso'])
    if c.get('linear', True): env['M4_ORACLE_LINEAR'] = '1'
    if not c.get('mag_linear', True): env['M4_ORACLE_MAG_FILTER'] = '0'
    if c['miplinear']: env['M4_ORACLE_MIP_LINEAR'] = '1'
    for key, option in [('minimum', 'SAMPLER_MIN'), ('maximum', 'SAMPLER_MAX'), ('bias', 'SAMPLER_BIAS'),
                        ('viewmin', 'VIEW_MIN_LOD'), ('layers', 'IMAGE_LAYERS')]:
        if key in c: env['M4_ORACLE_' + option] = str(c[key])
    if 'shader_min' in c: env['M4_ORACLE_MIN_LOD'] = '1'
    uvarg = 'vec3(uv,float(i%2u))' if layers > 1 else 'uv'
    sampler = 'sampler2DArray' if layers > 1 else 'sampler2D'
    if shadow: sampler='sampler2DShadow';uvarg='vec3(uv,.5)'
    clamp = 'ClampARB' if 'shader_min' in c else ''
    minarg = ',' + str(c['shader_min']) if clamp else ''
    extension = '#extension GL_ARB_sparse_texture_clamp : require\n' if clamp else ''
    shader = f'''#version 450
{extension}layout(local_size_x=64) in;
layout(set=0,binding=0) uniform {sampler} tex;
layout(set=0,binding=1,std430) writeonly buffer Output {{vec4 data[];}} dst;
void main() {{ uint i=gl_GlobalInvocationID.x;
 vec2 uv=(vec2(int(i%16u)-2,int(i/16u)-1)+(i%3u==0u?.5:.25))/16.;
 vec2 dx=vec2({2 ** c['lod'] / 16 * (1 if c.get('aniso_y') else c.get('aniso', 1))},0),dy=vec2(0,{2 ** c['lod'] / 16 * (c.get('aniso', 1) if c.get('aniso_y') else 1)});
 dst.data[i]=vec4(textureLod(tex,{uvarg},{float(c['lod'])}).r,
 textureGrad{clamp}(tex,{uvarg},dx,dy{minarg}).r,
 textureLodOffset(tex,{uvarg},{float(c['lod'])},ivec2(1,-1)).r,
 textureGradOffset{clamp}(tex,{uvarg},dx,dy,ivec2(1,-1){minarg}).r); }}
'''
    if sparse:
        shader=shader.replace('#version 450', '#version 450\n#extension GL_ARB_sparse_texture2 : require')
        start=shader.index(' dst.data[i]=')
        value_type='float' if shadow else 'vec4'
        sparse_suffix=clamp or 'ARB'
        calls=[f'sparseTextureLodARB(tex,{uvarg},{float(c["lod"])},v[0])',
               f'sparseTextureGrad{sparse_suffix}(tex,{uvarg},dx,dy{minarg},v[1])',
               f'sparseTextureLodOffsetARB(tex,{uvarg},{float(c["lod"])},ivec2(1,-1),v[2])',
               f'sparseTextureGradOffset{sparse_suffix}(tex,{uvarg},dx,dy,ivec2(1,-1){minarg},v[3])']
        shader=shader[:start]+f' {value_type} v[4]; int code[4];\n'+''.join(f' code[{i}]={call};\n' for i,call in enumerate(calls))
        shader+=' dst.data[i]=vec4('+','.join(f'sparseTexelsResidentARB(code[{i}])?v[{i}].r:-65536.' for i in range(4))+'); }\n'
    (root / (name + '.comp')).write_text(shader)
    subprocess.run(['glslangValidator', '-V', str(root / (name + '.comp')), '-o', str(root / (name + '.spv'))], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(root / (name + '.spv'))], check=True)
    c.update(name=name, env=env)
(root / 'cases.json').write_text(json.dumps(cases, indent=2) + '\n')
(root / 'run.py').write_text('''#!/usr/bin/env python3
import json,math,os,struct,sys,subprocess
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
failed=0
for c in json.loads((r/'cases.json').read_text()):
 name=c['name'];print('MINMAX_CASE:',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(name+'.spv')),str(r/(name+'-input.bin')),str(r/(name+'.bin')),'4',str(out/(name+'.bin'))],env=dict(os.environ,**c['env']),timeout=30,stdout=log,stderr=subprocess.STDOUT)
 good=p.returncode==0
 if c['mode']==0 and p.returncode in (0,1) and (out/(name+'.bin')).exists():
  actual=(out/(name+'.bin')).read_bytes();expected=(r/(name+'.bin')).read_bytes()
  good=len(actual)==len(expected) and actual[:16384]==expected[:16384] and actual[20480:]==expected[20480:]
  worst=0.
  if good:
   for offset in range(16384,20480,4):
    a=struct.unpack_from('<f',actual,offset)[0];e=struct.unpack_from('<f',expected,offset)[0]
    worst=max(worst,abs(a-e) if math.isfinite(a) else float('inf'))
   good=worst<=.001
  print(('PASS' if good else 'FAIL')+': weighted numeric check '+name+' max_error='+str(worst),flush=True)
 if c['mode']!=0: print(('PASS' if good else 'FAIL')+': exact minmax '+name,flush=True)
 failed+=not good
print(('FAIL' if failed else 'PASS')+': minmax sampler oracles',flush=True)
sys.exit(bool(failed))
''')
print(f'{len(cases)} exact minmax sampler probes generated in {root}')
