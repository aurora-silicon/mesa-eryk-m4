#!/usr/bin/env python3
"""Generate CPU, transfer and shader-producer sparse alias oracles.

Usage: sparse-alias-cases.py NEW_DIRECTORY
Compile *.comp with glslangValidator -V. Run generated run.py only inside the
workspace watchdog/thermal guard, with a built vk-buffer-oracle executable.
"""
import json
from pathlib import Path
import struct
import sys

root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=False)
n = 16384
values = [(i * 0x1234567 + 0x89abcdef) & 0xffffffff for i in range(n)]
for name, words in [('input', values + [0xdeadbeef] * n),
                    ('cpu-expected', values + values),
                    ('gpu-expected', [0x123abc] * (2 * n))]:
    (root / (name + '.bin')).write_bytes(struct.pack('<' + 'I' * len(words), *words))
read = '''#version 450
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) readonly buffer A {uint a[];};
layout(set=0,binding=1,std430) writeonly buffer B {uint b[];};
void main() { uint i=gl_GlobalInvocationID.x; b[i]=a[i]; }
'''
(root / 'read.comp').write_text(read)
(root / 'read-wide.comp').write_text(read.replace('b[i]=a[i]', 'b[16384+i]=a[i]'))
(root / 'write.comp').write_text('''#version 450
layout(local_size_x=64) in;
layout(set=0,binding=1,std430) buffer B {uint b[];};
void main() { b[gl_GlobalInvocationID.x]=0x123abcu; }
''')
(root / 'run.py').write_text('''#!/usr/bin/env python3
"""Run only within the external watchdog and thermal guard."""
import os
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parent
output = Path(sys.argv[1]); output.mkdir(parents=True, exist_ok=True)
oracle = sys.argv[2]
failed = 0
for name, shader, expected, extra in [
    ('cpu', 'read', 'cpu', {}),
    ('transfer', 'read', 'gpu', {'M4_ORACLE_SPARSE_FILL':'0x123abc'}),
    ('shader', 'read-wide', 'gpu', {'M4_ORACLE_SPARSE_SHADER':str(root/'write.spv')})]:
    env = dict(os.environ, M4_ORACLE_SPARSE_ALIAS='1', M4_ORACLE_BINDING_COUNT='2', **extra)
    print('SPARSE_ALIAS_CASE:', name, flush=True)
    result = subprocess.run([oracle, str(root/(shader+'.spv')), str(root/'input.bin'),
        str(root/(expected+'-expected.bin')), '256', str(output/(name+'.bin'))],
        env=env, timeout=20)
    failed += result.returncode != 0
print(('FAIL' if failed else 'PASS') + ': all sparse alias oracles', flush=True)
sys.exit(bool(failed))
''')
print(root)
