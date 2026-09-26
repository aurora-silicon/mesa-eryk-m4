#!/usr/bin/env python3
"""Generate retained FMA cases against host IEEE fmaf (no GPU execution).

Covers all 65536 half bit patterns, eight input-width combinations and four
source-modifier combinations. Explicit NoContraction decorations prevent the
frontend from splitting Fma and CSE-ing products across outputs. NaN payloads
are canonicalized; all other results, including signed zero, compare exactly.
expected-unfused.bin retains the two-rounding alternative for diagnostics.
Run vk-buffer-oracle with M4_ORACLE_FLOAT16=1 under the target watchdog/thermal
guard, using 2048 groups of 32 invocations.
"""
import argparse
import ctypes
import ctypes.util
import json
from pathlib import Path
import random
import re
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
source = ['#version 450', '#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require',
          'layout(local_size_x=32) in;',
          'layout(set=0,binding=0,std430) buffer B {uint w[];};',
          'void main(){uint base=gl_GlobalInvocationID.x*40u;']
for i in range(3):
    source += [f'float16_t h{i}=float16_t(unpackHalf2x16(w[base+{i}u]).x);',
               f'float f{i}=uintBitsToFloat(w[base+{i+3}u]);']
for mask in range(8):
    x, y, z = [f'float(h{i})' if mask & (1 << i) else f'f{i}' for i in range(3)]
    variants = [(x,y,z), ('-'+x,y,z), (x,'-'+y,z), ('-abs('+x+')','abs('+y+')','-'+z)]
    for mod, args in enumerate(variants):
        result = f'r{mask}_{mod}'
        source += [f'precise float {result}=fma({",".join(args)});',
                   f'w[base+{6+mask*4+mod}u]=isnan({result}) ? 0x7fc00000u : floatBitsToUint({result});']
source += ['}']
(a.output/'probe.comp').write_text('\n'.join(source)+'\n')
subprocess.run(['glslangValidator', '-V', str(a.output/'probe.comp'),
                '-o', str(a.output/'probe.spv')], check=True)
assembly = subprocess.check_output(['spirv-dis',str(a.output/'probe.spv')], text=True)
ids = re.findall(r'(%\w+) = OpExtInst %\w+ %\w+ Fma ',assembly)
assert len(ids) == 32
position = assembly.index('               OpDecorate')
decorations = ''.join(f'               OpDecorate {value} NoContraction\n'
                      for value in ids
                      if f'OpDecorate {value} NoContraction' not in assembly)
assembly = assembly[:position] + decorations + assembly[position:]
(a.output/'probe.spvasm').write_text(assembly)
subprocess.run(['spirv-as','--target-env','vulkan1.1',str(a.output/'probe.spvasm'),
                '-o',str(a.output/'probe.spv')], check=True)
subprocess.run(['spirv-val','--target-env','vulkan1.1',str(a.output/'probe.spv')], check=True)

lib = ctypes.CDLL(ctypes.util.find_library('m'))
fma = lib.fmaf
fma.argtypes = [ctypes.c_float]*3
fma.restype = ctypes.c_float
def half(bits):
    return struct.unpack('<e', struct.pack('<H', bits))[0]
def word(value):
    bits = struct.unpack('<I', struct.pack('<f', value))[0]
    return 0x7fc00000 if bits & 0x7fffffff > 0x7f800000 else bits
rng = random.Random(0xf16f32)
def native_mad(x,y,z):
    return ctypes.c_float(ctypes.c_float(x*y).value+z).value
initial, expected, fused = bytearray(), bytearray(), bytearray()
for i in range(65536):
    h = [i, (i*97+13) & 0xffff, (i*73+29) & 0xffff]
    # F32 inputs are normals or signed zero: half subnormals widen to normals.
    f = [(rng.getrandbits(1)<<31) | (rng.randrange(108,147)<<23) | rng.getrandbits(23)
         for _ in range(3)]
    if i % 17 == 0:
        f[i % 3] = (i & 1) << 31
    header = h + f
    result, ieee = [], []
    for mask in range(8):
        x,y,z = [half(h[j]) if mask & (1<<j) else struct.unpack('<f',struct.pack('<I',f[j]))[0]
                 for j in range(3)]
        args = [(x,y,z), (-x,y,z), (x,-y,z), (-abs(x),abs(y),-z)]
        result += [word(native_mad(*v)) for v in args]
        ieee += [word(fma(*v)) for v in args]
    initial.extend(struct.pack('<40I', *(header+[0xdeadbeef]*32+[0,0])))
    fused.extend(struct.pack('<40I', *(header+ieee+[0,0])))
    expected.extend(struct.pack('<40I', *(header+result+[0,0])))
(a.output/'input.bin').write_bytes(initial)
(a.output/'expected-unfused.bin').write_bytes(expected)
(a.output/'expected.bin').write_bytes(fused)
(a.output/'manifest.json').write_text(json.dumps(dict(groups=2048, half_patterns=65536,
    results=65536*32, reference='IEEE fmaf, NaN payloads canonicalized; alternative two-rounding model retained',
    gpu_executed=False), indent=2)+'\n')
