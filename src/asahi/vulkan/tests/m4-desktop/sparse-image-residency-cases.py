#!/usr/bin/env python3
"""Generate sparse depth compare/gather and fully-bound MS query oracles.

Usage: sparse-image-residency-cases.py NEW_DIRECTORY
Run the generated runner only under gpu-experiment.py's watchdog/thermal guard.
"""
from pathlib import Path
import struct
import subprocess
import sys
root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=False)
initial = bytearray(32768)
for i in range(256):
    struct.pack_into('<f', initial, i * 4, .75 if (i % 16 + i // 16) % 2 == 0 else .25)
(root / 'input.bin').write_bytes(initial)
ms = bytearray(32768)
struct.pack_into('<4I', ms, 0, 7, 8, 9, 10)
(root / 'multisample-input.bin').write_bytes(ms)
for name in ['level', 'gather', 'multisample']:
    expected = bytearray(ms if name == 'multisample' else initial)
    for i in range(256):
        x, y = i % 16, i // 16
        if name == 'level':
            v = float((i % 16 + i // 16) % 2 == 0)
            values = (v, 1., v, 1.)
        elif name == 'gather':
            # uv=(pixel+.25)/16 gives base floor(pixel-.25).
            def compare(px, py):
                px = max(0, min(15, px)); py = max(0, min(15, py))
                return float((py + px) % 2 == 0)
            values = tuple(compare(x + dx, y + dy) for dx, dy in
                           [(-1, 0), (0, 0), (0, -1), (-1, -1)])
        else:
            values = (7, 1, 0, 1)
        struct.pack_into('<4I' if name == 'multisample' else '<4f',
                         expected, 16384 + i * 16, *values)
    (root / (name + '.bin')).write_bytes(expected)
(root / 'level.comp').write_text('#version 450\n#extension GL_ARB_sparse_texture2 : require\nlayout(local_size_x=64) in;\nlayout(set=0,binding=0) uniform sampler2DShadow tex;\nlayout(set=0,binding=1,std430) writeonly buffer Out {vec4 data[];} dst;\nvoid main() { uint i=gl_GlobalInvocationID.x; uvec2 p=uvec2(i%16u,i/16u); vec2 uv=(vec2(p)+.25)/16.; float v; int code=sparseTextureLodARB(tex,vec3(uv,.5),0.0,v); dst.data[i]=vec4(v,float(sparseTexelsResidentARB(code)),textureLod(tex,vec3(uv,.5),0.0),1); }\n')
(root / 'gather.comp').write_text('#version 450\n#extension GL_ARB_sparse_texture2 : require\nlayout(local_size_x=64) in;\nlayout(set=0,binding=0) uniform sampler2DShadow tex;\nlayout(set=0,binding=1,std430) writeonly buffer Out {vec4 data[];} dst;\nvoid main() { uint i=gl_GlobalInvocationID.x; uvec2 p=uvec2(i%16u,i/16u); vec2 uv=(vec2(p)+.25)/16.; vec4 v; int code=sparseTextureGatherARB(tex,uv,.5,v); dst.data[i]=sparseTexelsResidentARB(code)?v:vec4(-1); }\n')
(root / 'multisample.comp').write_text('#version 450\n#extension GL_ARB_sparse_texture2 : require\nlayout(local_size_x=64) in;\nlayout(set=0,binding=0) uniform usampler2DMS tex;\nlayout(set=0,binding=1,std430) writeonly buffer Out {uvec4 data[];} dst;\nvoid main() { uint i=gl_GlobalInvocationID.x; ivec2 p=ivec2(i%16u,i/16u); uvec4 v; int code=sparseTexelFetchARB(tex,p,int(i%4u),v); dst.data[i]=uvec4(v.x,uint(sparseTexelsResidentARB(code)),v.y,v.w); }\n')
for name in ['level', 'gather', 'multisample']:
    subprocess.run(['glslangValidator', '-V', str(root / (name + '.comp')),
                    '-o', str(root / (name + '.spv'))], check=True)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.1',
                    str(root / (name + '.spv'))], check=True)
# Constant face colors isolate cube projection and the sparse result layout.
cube = bytearray(32768)
for face in range(6):
    for pixel in range(256):
        struct.pack_into('<f', cube, (face * 256 + pixel) * 4, face + .25)
(root / 'cube-input.bin').write_bytes(cube)
for name in ['cube-level', 'cube-gather']:
    shader = """#version 450
#extension GL_ARB_sparse_texture2 : require
layout(local_size_x=64) in;
layout(set=0,binding=0) uniform samplerCube tex;
layout(set=0,binding=1,std430) writeonly buffer Out {vec4 data[];} dst;
void main() {
 uint i=gl_GlobalInvocationID.x; uint face=i%6u;
 vec3 dirs[6]=vec3[6](vec3(1,0,0),vec3(-1,0,0),vec3(0,1,0),vec3(0,-1,0),vec3(0,0,1),vec3(0,0,-1));
 vec4 v; OPERATION
 dst.data[i]=RESULT;
}
"""
    operation = ('int code=sparseTextureLodARB(tex,dirs[face],0.,v);' if name == 'cube-level'
                 else 'int code=sparseTextureGatherARB(tex,dirs[face],v,0);')
    result = ('vec4(v.x,float(sparseTexelsResidentARB(code)),v.y,v.w)' if name == 'cube-level'
              else 'sparseTexelsResidentARB(code)?v:vec4(-1)')
    (root / (name + '.comp')).write_text(shader.replace('OPERATION', operation).replace('RESULT', result))
    expected = bytearray(cube)
    for i in range(256):
        v = i % 6 + .25
        struct.pack_into('<4f', expected, 16384 + i * 16,
                         *((v, 1., 0., 1.) if name == 'cube-level' else (v,) * 4))
    (root / (name + '.bin')).write_bytes(expected)
    subprocess.run(['glslangValidator', '-V', str(root / (name + '.comp')),
                    '-o', str(root / (name + '.spv'))], check=True)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(root / (name + '.spv'))], check=True)
scaled = bytearray(32768)
for i in range(256): scaled[i] = i
(root / 'scaled-input.bin').write_bytes(scaled)
expected = bytearray(scaled)
for i in range(256): struct.pack_into('<4f', expected, 16384 + i * 16, float(i), 1., 0., 1.)
(root / 'scaled.bin').write_bytes(expected)
(root / 'scaled.comp').write_text("""#version 450
#extension GL_ARB_sparse_texture2 : require
layout(local_size_x=64) in;
layout(set=0,binding=0) uniform sampler2D tex;
layout(set=0,binding=1,std430) writeonly buffer Out {vec4 data[];} dst;
void main() { uint i=gl_GlobalInvocationID.x; vec2 uv=(vec2(i%16u,i/16u)+.25)/16.; vec4 v;
 int code=sparseTextureLodARB(tex,uv,0.,v); dst.data[i]=vec4(v.x,float(sparseTexelsResidentARB(code)),v.y,v.w); }
""")
subprocess.run(['glslangValidator','-V',str(root/'scaled.comp'),'-o',str(root/'scaled.spv')],check=True)
subprocess.run(['spirv-val','--target-env','vulkan1.1',str(root/'scaled.spv')],check=True)
(root / 'run.py').write_text("#!/usr/bin/env python3\nimport os,sys,subprocess\nfrom pathlib import Path\nr=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)\nfailed=0\nfor name in ['level','gather','multisample','cube-level','cube-gather','scaled']:\n env=dict(os.environ,M4_ORACLE_BINDING_COUNT='2',M4_ORACLE_RESIDENCY='1',M4_ORACLE_IMAGE_FORMAT='11' if name=='scaled' else '100' if name.startswith('cube-') else '98' if name=='multisample' else '126')\n if name!='scaled': env['M4_ORACLE_IMAGE_CUBE' if name.startswith('cube-') else 'M4_ORACLE_IMAGE_MS4' if name=='multisample' else 'M4_ORACLE_IMAGE_DEPTH']='1'\n print('SPARSE_IMAGE_CASE:',name,flush=True)\n p=subprocess.run([sys.argv[2],str(r/(name+'.spv')),str(r/('scaled-input.bin' if name=='scaled' else 'cube-input.bin' if name.startswith('cube-') else 'multisample-input.bin' if name=='multisample' else 'input.bin')),str(r/(name+'.bin')),'4',str(out/(name+'.bin'))],env=env,timeout=20)\n failed+=p.returncode!=0\nprint(('FAIL' if failed else 'PASS')+': sparse image residency oracles',flush=True)\nsys.exit(bool(failed))\n")
