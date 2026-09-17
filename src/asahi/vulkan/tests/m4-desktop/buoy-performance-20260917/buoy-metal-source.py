#!/usr/bin/env python3
"""Translate the retained Buoy GLSL body to Metal for offline M4 comparison.

Keeps the shader body and Shadertoy main wrapper; removes unused header uniforms
and adapts the uniform bindings through glslang/SPIRV-Cross. This is not an ANGLE
macOS capture or proof of equivalent native rendering/performance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--glslang', default='glslangValidator')
p.add_argument('--spirv-cross', required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
body = (a.source / 'Buoy.glsl').read_text()
header = (a.source / 'header.glsl').read_text()
assert set(re.findall(r'\bi[A-Z][A-Za-z0-9_]+', body)) == {
    'iResolution', 'iTime', 'iMouse', 'iChannel0'}
assert '#define HW_PERFORMANCE 1' in header
prefix = ('#version 450\n#define HW_PERFORMANCE 1\n'
          'uniform vec3 iResolution;\nuniform float iTime;\n'
          'uniform vec4 iMouse;\nuniform sampler2D iChannel0;\n')
(a.output / 'buoy.frag').write_text(prefix + header[header.index('void mainImage'):] + '\n' + body)
commands = [
    [a.glslang, '-V', '-R', '--auto-map-bindings', '--auto-map-locations',
     '--set-default-uniform-block', 'BuoyUniforms', '0', '0',
     str(a.output / 'buoy.frag'), '-o', str(a.output / 'buoy.spv')],
    [a.spirv_cross, str(a.output / 'buoy.spv'), '--msl', '--msl-version', '20100',
     '--output', str(a.output / 'buoy.metal')],
]
records = []
for command in commands:
    result = subprocess.run(command, text=True, capture_output=True, timeout=60)
    records.append(dict(argv=command, returncode=result.returncode,
                        stdout=result.stdout, stderr=result.stderr))
    (a.output / 'commands.json').write_text(json.dumps(records, indent=2) + '\n')
    result.check_returncode()
metal = a.output / 'buoy.metal'
metal.write_text(metal.read_text() + '\nstruct BuoyVertex { float4 position [[position]]; };\n'
                 'vertex BuoyVertex vs(uint id [[vertex_id]]) { float2 p=float2((id<<1)&2,id&2); '
                 'return {float4(p*2.0-1.0,0,1)}; }\n')
paths = [a.source / 'Buoy.glsl', a.source / 'header.glsl',
         Path(shutil.which(a.glslang) or a.glslang), Path(a.spirv_cross),
         a.output / 'buoy.frag', a.output / 'buoy.spv', metal]
(a.output / 'sha256.json').write_text(json.dumps(
    {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}, indent=2) + '\n')
print(metal)
