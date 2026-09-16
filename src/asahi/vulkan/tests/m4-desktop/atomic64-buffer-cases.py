#!/usr/bin/env python3
"""Independent Vulkan buffer atomic64 operation and returned-value oracles.

Generate with NEW_DIRECTORY. Run generated run.py only under the M4 guard.
"""
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=False)
shared = 'shared' in sys.argv[2:]
mask = (1 << 64) - 1
rng = random.Random(0x64164)
edge = [0, mask, 1 << 63, (1 << 63)-1, 0xffffffff, 0x100000000,
        0x0123456789abcdef, 0xfedcba9876543210]
values = edge * 4 + [rng.getrandbits(64) for _ in range(32)]
args = [edge[(i+3) % len(edge)] if i < 32 else rng.getrandbits(64) for i in range(64)]
def signed(v): return v if v < 1 << 63 else v - (1 << 64)
cases = []
for op in ['add', 'min', 'max', 'smin', 'smax', 'and', 'or', 'xor', 'exchange', 'compare_swap']:
    signed_op = op.startswith('s')
    ty = 'int64_t' if signed_op else 'uint64_t'
    fn = {'smin':'Min', 'smax':'Max', 'compare_swap':'CompSwap'}.get(op, op.title())
    initial = values + args + [0xdeadc0dedeadc0de] * 128
    expected = list(initial)
    for i,(old,arg) in enumerate(zip(values,args)):
        if op == 'add': result = old + arg
        elif op in ('min','max'): result = (min if op == 'min' else max)(old,arg)
        elif signed_op: result = (min if op == 'smin' else max)(signed(old),signed(arg))
        elif op == 'and': result = old & arg
        elif op == 'or': result = old | arg
        elif op == 'xor': result = old ^ arg
        elif op == 'exchange': result = arg
        else: result = arg if i % 2 == 0 else old
        expected[i] = result & mask
        expected[128+i] = old
    for suffix, contents in [('input', initial),('expected',expected)]:
        (root/f'{op}-{suffix}.bin').write_bytes(struct.pack('<256Q', *contents))
    operand = 'src.data[64u+i]'
    if op == 'compare_swap': operand = '(i&1u)==0u?src.data[i]:~src.data[i],src.data[64u+i]'
    source = f'''#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_atomic_int64 : require
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) buffer Data {{{ty} data[];}} src;
void main(){{uint i=gl_GlobalInvocationID.x;src.data[128u+i]=atomic{fn}(src.data[i],{operand});}}
'''
    if shared:
        # Preserve the same independent expected results, but operate on
        # initialized workgroup memory and publish the final shared value.
        source = source.replace('void main()', 'shared '+ty+' local_data[64];\nvoid main()')
        source = source.replace('uint i=gl_GlobalInvocationID.x;',
          'uint i=gl_GlobalInvocationID.x;local_data[i]=src.data[i];barrier();')
        source = source.replace('atomic'+fn+'(src.data[i],', 'atomic'+fn+'(local_data[i],')
        source = ';barrier();src.data[i]=local_data[i];}'.join(source.rsplit(';}', 1))
    (root/f'{op}.comp').write_text(source)
    subprocess.run(['glslangValidator','-V',str(root/f'{op}.comp'),'-o',str(root/f'{op}.spv')],check=True)
    subprocess.run(['spirv-val','--target-env','vulkan1.1',str(root/f'{op}.spv')],check=True)
    cases.append(dict(name=op, groups=1, env={'M4_ORACLE_ATOMIC64': 'shared' if shared else '1'}))
(root/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
(root/'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
failed=0
for c in json.loads((r/'cases.json').read_text()):
 name=c['name'];print('ATOMIC64_CASE:',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(name+'.spv')),str(r/(name+'-input.bin')),str(r/(name+'-expected.bin')),str(c['groups']),str(out/(name+'.bin'))],env=dict(os.environ,**c['env']),stdout=log,stderr=subprocess.STDOUT,timeout=20)
 print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True);failed+=p.returncode!=0
 if p.returncode:break
print(('FAIL' if failed else 'PASS')+': ten buffer atomic64 operations and returned values',flush=True)
sys.exit(bool(failed))
''')
if shared:
    p=root/'run.py';p.write_text(p.read_text().replace('ten buffer atomic64','ten shared atomic64'))
print(root,len(cases))
