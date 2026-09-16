#!/usr/bin/env python3
"""Generate row-crossing texel-view oracles for vk-buffer-oracle.c (no GPU work).

Usage: texel-alignment-cases.py NEW_DIRECTORY
Compile shader.comp files with glslangValidator -V. Copy the directory and the
rebuilt oracle to the target, then run run.py only under gpu-experiment.py.
Input/output bindings occupy disjoint halves of one coherent allocation.
"""
import json
from pathlib import Path
import struct
import sys

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=False)
count, half = 16384, 512 * 1024
cases = []
for name, fmt, size, channels, floating, offsets in [
    ("r8ui", 13, 1, 1, False, [1, 15]),
    ("r32ui", 98, 4, 1, False, [4, 12]),
    ("rgb32ui", 104, 12, 3, False, [4, 8, 12]),
    ("rgb32f", 106, 12, 3, True, [4, 8, 12]),
]:
    modes = ["fetch"] + (["store"] if channels == 1 else [])
    if fmt == 98:
        modes.append("atomic")
    for offset in offsets:
        for mode in modes:
            label = f"{name}-{offset}-{mode}"
            directory = out / label
            directory.mkdir()
            initial = bytearray(b"\xcd" * (2 * half))
            values = []
            for i in range(count):
                pixel = []
                for c in range(channels):
                    v = ((i * 37 + c * 53 + 19) % 251) if size == 1 else (
                        ((i * 7 + c * 11) % 97) / 8 if floating else 0x91300000 + i * 3 + c)
                    struct.pack_into("<B" if size == 1 else "<f" if floating else "<I",
                                     initial, offset + i * size + c * 4, v)
                    pixel.append(v)
                values.append(pixel)
            expected = bytearray(initial)
            for i, pixel in enumerate(values):
                if mode == "store":
                    pixel = [(i * 37 + 19) % 251]
                    struct.pack_into("<B" if size == 1 else "<I", expected,
                                     offset + i * size, pixel[0])
                elif mode == "atomic":
                    struct.pack_into("<I", expected, offset + i * size, pixel[0] + 7)
                result = pixel + [0] * (3 - channels) + [1]
                if floating:
                    result = [struct.unpack("<I", struct.pack("<f", x))[0] for x in result]
                if i == 0:
                    result[3] = count
                struct.pack_into("<4I", expected, half + i * 16, *result)
            declaration = (f"layout(set=0,binding=0) uniform {'samplerBuffer' if floating else 'usamplerBuffer'} src;"
                           if mode == "fetch" else
                           f"layout(set=0,binding=0,{name}) uniform uimageBuffer src;")
            body = ("uvec4 v=floatBitsToUint(texelFetch(src,int(i)));" if floating else
                    "uvec4 v=texelFetch(src,int(i));") if mode == "fetch" else (
                    "imageStore(src,int(i),uvec4((i*37u+19u)%251u,0,0,1)); memoryBarrierImage(); uvec4 v=imageLoad(src,int(i));"
                    if mode == "store" else
                    "uvec4 v=uvec4(imageAtomicAdd(src,int(i),7u),0,0,1);")
            query = "textureSize(src)" if mode == "fetch" else "imageSize(src)"
            shader = f"""#version 450
layout(local_size_x=64) in;
{declaration}
layout(set=0,binding=1,std430) buffer Output {{ uvec4 data[]; }} dst;
void main() {{ uint i=gl_GlobalInvocationID.x; {body}
if(i==0) v.w=uint({query}); dst.data[i]=v; }}
"""
            (directory / "shader.comp").write_text(shader)
            (directory / "input.bin").write_bytes(initial)
            (directory / "expected.bin").write_bytes(expected)
            cases.append({"name": label, "groups": count // 64, "format": fmt,
                          "offset": offset, "range": count * size,
                          "storage": mode != "fetch"})
(out / "cases.json").write_text(json.dumps(cases, indent=2) + "\n")
(out / "run.py").write_text('''#!/usr/bin/env python3
# Execute only inside gpu-experiment.py's mandatory guard.
import json, os
from pathlib import Path
import subprocess, sys
root=Path(__file__).resolve().parent
output=Path(sys.argv[1]); output.mkdir(parents=True,exist_ok=True)
for case in json.loads((root/'cases.json').read_text()):
    d=root/case['name']
    env={**os.environ, 'M4_ORACLE_BINDING_COUNT':'2',
         'M4_ORACLE_TEXEL_FORMAT':str(case['format']),
         'M4_ORACLE_TEXEL_OFFSET':str(case['offset']),
         'M4_ORACLE_TEXEL_RANGE':str(case['range']),
         'M4_ORACLE_TEXEL_STORAGE':str(int(case['storage']))}
    print('CASE:',case['name'],flush=True)
    subprocess.run([sys.argv[2],str(d/'shader.spv'),str(d/'input.bin'),
                    str(d/'expected.bin'),str(case['groups']),
                    str(output/(case['name']+'.bin'))],env=env,check=True,timeout=25)
print('PASS: all texel alignment oracles',flush=True)
''')
print(f"Generated {len(cases)} oracles in {out}")
