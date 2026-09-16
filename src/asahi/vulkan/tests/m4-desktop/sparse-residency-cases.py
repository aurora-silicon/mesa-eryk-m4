#!/usr/bin/env python3
"""Generate sparse texel-buffer residency/rebind and offset oracles.

Usage: sparse-residency-cases.py NEW_DIRECTORY
Builds SPIR-V with glslangValidator and SPIR-V Tools. Run run.py only under the mandatory
watchdog/thermal guard. Requires vk-buffer-oracle with sparse texel mask support.
"""
import json
from pathlib import Path
import struct
import re
import subprocess
import sys

root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=False)
half = 131072
count = 1024
initial = bytearray(half * 2)
for i in range(len(initial) // 4):
    struct.pack_into('<I', initial, i * 4, (i * 0x1234567 + 0x89abcdef) & 0xffffffff)
(root / 'input.bin').write_bytes(initial)
cases = []
for fmt, vkformat, size in [('r32ui', 98, 4), ('rgba32ui', 107, 16), ('rgb32ui', 104, 12)]:
    for storage in ([False, True] if size != 12 else [False]):
        for offset in [4, 16380, 32772] + ([16368] if size == 16 else []):
            high = (1 << 32) if offset == 16368 else 0
            # 16-byte texels need a complete texel offset.
            if size == 16:
                offset = (offset // 16) * 16
            elements = (16384 * 16384 - 15) if high else 32760 // size
            for rebind in ([False] if high else [False, True]):
                name = ('high-' if high else '') + f'{fmt}-{int(storage)}-{offset}-' + ('rebind' if rebind else 'initial')
                final_mask = 0xaa if rebind else 0x55
                expected = bytearray(initial)
                for i in range(count):
                    coord = 268434432 + i % 64 if high else (i * 53) % elements
                    address = offset + coord * size
                    resident = all(page >= 0 and final_mask & (1 << page) for page in range((address - high) // 16384, (address - high + size - 1) // 16384 + 1))
                    value = struct.unpack_from('<I', initial, address - high)[0] if resident else 0x12345678
                    struct.pack_into('<II', expected, half + i * 8, value, int(resident))
                (root / (name + '.bin')).write_bytes(expected)
                declaration = (f'layout(set=0,binding=0,{fmt}) uniform readonly uimageBuffer src;' if storage else
                               'layout(set=0,binding=0) uniform usamplerBuffer src;')
                operation = 'imageLoad' if storage else 'texelFetch'
                (root / (name + '.comp')).write_text(f'''#version 450
layout(local_size_x=64) in;
{declaration}
layout(set=0,binding=1,std430) writeonly buffer Output {{ uvec2 data[]; }} dst;
void main() {{
 uint i=gl_GlobalInvocationID.x;
 int coord=int({'268434432u+i%64u' if high else f'(i*53u)%{elements}u'});
 uvec4 value={operation}(src,coord);
 bool resident=true;
 dst.data[i]=uvec2(resident?value.x:0x12345678u,uint(resident));
}}
''')
                source = root / (name + '.comp')
                binary = source.with_suffix('.spv')
                subprocess.run(['glslangValidator', '-V', str(source), '-o', str(binary)],
                               check=True, stdout=subprocess.DEVNULL)
                assembly = subprocess.check_output(['spirv-dis', str(binary)], text=True)
                # GLSL has no sparse buffer overloads (same limitation documented
                # in CTS BufferSparseResidency). Preserve the generated control
                # flow and replace exactly the image operation with its SPIR-V
                # sparse equivalent plus the required residency query.
                assembly = assembly.replace('OpCapability Shader', 'OpCapability Shader\n               OpCapability SparseResidency', 1)
                match = re.search(r'(?m)^\s*(%\w+) = (OpImageFetch|OpImageRead) (%\w+) (.+)$', assembly)
                assert match and not re.search(r'OpImageFetch|OpImageRead', assembly[match.end():])
                result, op, result_type, operands = match.groups()
                replacement = (f'%sparse_result = {op.replace("OpImage", "OpImageSparse")} %sparse_type {operands}\n'
                               f'{result} = OpCompositeExtract {result_type} %sparse_result 1\n'
                               '%sparse_code = OpCompositeExtract %uint %sparse_result 0')
                assembly = assembly[:match.start()] + replacement + assembly[match.end():]
                entry = re.search(r'(?m)^\s*%main = OpFunction ', assembly)
                assert entry
                assembly = assembly[:entry.start()] + '%sparse_type = OpTypeStruct %uint ' + result_type + '\n' + assembly[entry.start():]
                assert 'OpStore %resident %true' in assembly
                assembly = assembly.replace('OpStore %resident %true',
                    '%resident_value = OpImageSparseTexelsResident %bool %sparse_code\n               OpStore %resident %resident_value', 1)
                asm = source.with_suffix('.spvasm'); asm.write_text(assembly)
                subprocess.run(['spirv-as', '--target-env', 'vulkan1.1', str(asm), '-o', str(binary)], check=True)
                subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(binary)], check=True)
                env = dict(M4_ORACLE_BINDING_COUNT='2', M4_ORACLE_TEXEL_FORMAT=str(vkformat),
                           M4_ORACLE_TEXEL_OFFSET=str(offset), M4_ORACLE_TEXEL_RANGE=str(elements * size),
                           M4_ORACLE_TEXEL_STORAGE=str(int(storage)), M4_ORACLE_SPARSE_TEXEL_MASK='0x55')
                if high:
                    env['M4_ORACLE_SPARSE_HIGH_OFFSET'] = str(high)
                if rebind:
                    env['M4_ORACLE_SPARSE_REBIND_MASK']='0xaa'
                cases.append(dict(name=name, env=env))
(root / 'cases.json').write_text(json.dumps(cases, indent=2)+'\n')
(root / 'run.py').write_text('''#!/usr/bin/env python3
"""Run only within the external watchdog and thermal guard."""
import json, os, subprocess, sys
from pathlib import Path
root=Path(__file__).resolve().parent
out=Path(sys.argv[1]); out.mkdir(parents=True,exist_ok=True)
failed=0
for case in json.loads((root/'cases.json').read_text()):
 name=case['name']; print('SPARSE_RESIDENCY_CASE:',name,flush=True)
 r=subprocess.run([sys.argv[2],str(root/(name+'.spv')),str(root/'input.bin'),
     str(root/(name+'.bin')),'16',str(out/(name+'.bin'))],
     env=dict(os.environ,**case['env']),timeout=20)
 failed+=r.returncode!=0
print(('FAIL' if failed else 'PASS')+': all sparse residency oracles',flush=True)
sys.exit(bool(failed))
''')
print(root, len(cases))
