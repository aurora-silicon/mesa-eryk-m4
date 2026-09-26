#!/usr/bin/env python3
"""Generate readonly scalar-load tests with partial SSBO descriptors.

No GPU execution here. Copy the output to a target and run run.py under its
watchdog/thermal guard, passing an output directory and vk-buffer-oracle binary.
The helper requires M4_ORACLE_SSBO0_RANGE and two ordinary buffer bindings.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
source = '''#version 450
layout(local_size_x=32) in;
layout(set=0,binding=0,std430) readonly buffer A {uvec4 a[];};
layout(set=0,binding=1,std430) writeonly buffer B {uint b[];};
void main() {
 uint lane=gl_GlobalInvocationID.x;
 uint idx=lane==31 ? 0x0fffffffu : lane;
 uint x=a[idx].x, y=a[idx].y, z=a[idx].z, w=a[idx].w;
 b[lane*4u]=x+y+z+w; b[lane*4u+1u]=x;
 b[lane*4u+2u]=y; b[lane*4u+3u]=z;
}
'''
(a.output/'probe.comp').write_text(source)
subprocess.run(['glslangValidator', '-V', str(a.output/'probe.comp'),
                '-o', str(a.output/'probe.spv')], check=True)
data = [(i*0x7654321 ^ 0xaabbccdd) & 0xffffffff for i in range(128)]
(a.output/'input.bin').write_bytes(struct.pack('<256I', *(data+[0xdeadbeef]*128)))
bounds = [1, 3, 4, 7, 8, 12, 15, 16, 17, 31, 32, 63, 64, 127]
for bound in bounds:
    expected = data.copy()
    for lane in range(32):
        idx = 0x3ffffffc if lane == 31 else lane*4
        values = [data[idx+c] if (idx+c+1)*4 <= bound else 0 for c in range(4)]
        expected += [sum(values) & 0xffffffff, *values[:3]]
    (a.output/f'expected-{bound}.bin').write_bytes(struct.pack('<256I', *expected))
(a.output/'bounds.json').write_text(json.dumps(bounds)+'\n')
(a.output/'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
for bound in json.loads((root/'bounds.json').read_text()):
 env=dict(os.environ,M4_ORACLE_BINDING_COUNT='2',M4_ORACLE_SSBO0_RANGE=str(bound),M4_ORACLE_ROBUSTNESS2='1')
 subprocess.run([sys.argv[2],str(root/'probe.spv'),str(root/'input.bin'),str(root/f'expected-{bound}.bin'),'1',str(out/f'actual-{bound}.bin')],env=env,check=True)
print('PASS: 14 partial descriptor ranges, including 4-GiB offset boundary',flush=True)
''')
