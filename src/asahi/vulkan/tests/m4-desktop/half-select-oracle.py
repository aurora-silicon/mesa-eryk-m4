#!/usr/bin/env python3
"""Reduce WebGL half-coordinate selections to an exact compute buffer oracle.

No GPU runs here. Execute probe.spv with vk-buffer-oracle and
M4_ORACLE_FLOAT16=1; hardware execution requires gpu-experiment.py.
"""
import argparse, hashlib, json, struct, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a=p.parse_args();a.output.mkdir(parents=True, exist_ok=False)
SOURCE = '#version 450\n#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\nlayout(local_size_x=32) in;\nlayout(set=0,binding=0,std430) buffer Data { uint v[]; } data;\nvoid main(){uint o=gl_GlobalInvocationID.x*8;float y=uintBitsToFloat(data.v[o]);\nfloat16_t sy=float16_t(y*-1.+480.);\nfloat16_t g=sy<float16_t(240.)?float16_t(64.):float16_t(128.);\nfloat16_t r=sy<float16_t(240.)?float16_t(191.):float16_t(32.);\ndata.v[o+1]=floatBitsToUint(float(sy));data.v[o+2]=floatBitsToUint(float(g));\ndata.v[o+3]=floatBitsToUint(float(g/float16_t(255.)));\ndata.v[o+4]=floatBitsToUint(float(r));data.v[o+5]=floatBitsToUint(float(r/float16_t(255.)));\ndata.v[o+6]=floatBitsToUint(float(float16_t(y)));data.v[o+7]=floatBitsToUint(y*-1.+480.);\n}\n'
(a.output/'probe.comp').write_text(SOURCE)
def half(x): return struct.unpack('<e',struct.pack('<e',x))[0]
initial=[];expected=[]
for lane in range(256):
 y=lane*2+.5;sy=half(480-y)
 g=64. if sy<240. else 128.
 r=191. if sy<240. else 32.
 initial.extend([y]+[0.]*7)
 expected.extend([y,sy,g,half(g/255.),r,half(r/255.),half(y),480-y])
for name,values in [('input',initial),('expected',expected)]:
 (a.output/(name+'.bin')).write_bytes(struct.pack('<2048f',*values))
cmd=['glslangValidator','-V',str(a.output/'probe.comp'),'-o',str(a.output/'probe.spv')]
r=subprocess.run(cmd,text=True,capture_output=True)
(a.output/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
manifest={'workgroups':8,'lanes':256,'words':2048,'gpu_executed':False,
 'required_environment':{'M4_ORACLE_FLOAT16':'1'},
 'reference':'Python IEEE binary16 rounding of coordinate, selection and division',
 'fields':['input_y','half_y','green_select','green_divide','red_select','red_divide','half_input','float_y'],
 'command':cmd,'sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in a.output.iterdir() if f.is_file()}}
(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest))
