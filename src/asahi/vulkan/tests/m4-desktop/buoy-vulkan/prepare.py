#!/usr/bin/env python3
"""Package the retained unmodified Buoy GLSL and texture for the Vulkan player."""
import argparse, hashlib, json, subprocess
from pathlib import Path
from PIL import Image
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workspace-root',type=Path,default=Path(__file__).resolve().parents[2])
parser.add_argument('--output',type=Path)
a=parser.parse_args()
root=a.workspace_root.resolve()
assets=root/'tools/shadertoy-buoy'
out=a.output or root/'build/buoy-vulkan';out.mkdir(parents=True,exist_ok=True)
source=(assets/'Buoy.glsl').read_bytes()
manifest=json.loads((assets/'manifest.json').read_text())
assert hashlib.sha256(source).hexdigest()==manifest['source_sha256']
assert hashlib.sha256((assets/'channel0.png').read_bytes()).hexdigest()==manifest['texture_sha256']
(out/'buoy.frag').write_text('''#version 450
#define HW_PERFORMANCE 1
layout(set=0,binding=0) uniform sampler2D iChannel0;
layout(push_constant) uniform State { float time; } state;
#define iResolution vec3(3840.0,2160.0,1.0)
#define iTime state.time
#define iMouse vec4(0.0)
layout(location=0) out vec4 color;
'''+source.decode()+'''
void main() {
    mainImage(color, vec2(gl_FragCoord.x, 2160.0-gl_FragCoord.y));
    color.a=1.0;
}
''')
(out/'buoy.vert').write_text('''#version 450
void main() {
 vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
 gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
''')
for stage in ('vert','frag'):
 subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1','-o',str(out/f'buoy.{stage}.spv'),str(out/f'buoy.{stage}')],check=True)
 subprocess.run(['spirv-val','--target-env','vulkan1.1',str(out/f'buoy.{stage}.spv')],check=True)
# Match WebGL UNPACK_FLIP_Y_WEBGL=false and byte/linear texture decoding.
im=Image.open(assets/'channel0.png').convert('RGBA');assert im.size==(256,256)
(out/'noise.rgba').write_bytes(im.tobytes())
manifest['render_extent']=[3840,2160]
manifest['artifacts']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [out/'buoy.vert.spv',out/'buoy.frag.spv',out/'noise.rgba']}
(out/'assets.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(out)
