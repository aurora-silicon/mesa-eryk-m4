#!/usr/bin/env python3
"""Generate exact signed byte-unpack and truncating-shift tests; no GPU run.

Use vk-buffer-oracle under the target watchdog/thermal guard. Includes INT_MIN,
all 256 byte patterns, mixed bytes, shifts 0..31 and three sign formulations.
"""
import argparse,hashlib,json,random,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
values=[0,1,0xffffffff,0x7fffffff,0x80000000,0x80000001,0x7ffffffe]
values += [x*0x01010101 for x in range(256)]
rng=random.Random(0x81332)
values += [rng.getrandbits(32) for _ in range(512-len(values))]
stride=1+4+3*32
source=['#version 450','#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require','layout(local_size_x=32) in;','layout(set=0,binding=0,std430) buffer B {uint w[];};',f'void main(){{uint b=gl_GlobalInvocationID.x*{stride}u;uint u=w[b];int x=int(u);','ivec4 bytes=ivec4(unpack8(int(u)));']
for c in range(4):source.append(f'w[b+{c+1}u]=uint(bytes[{c}]);')
for variant,sign in enumerate(['sign(x)','min(max(x,-1),1)','max(min(x,1),-1)']):
 for k in range(32):source.append(f'w[b+{5+variant*32+k}u]=uint({sign})*(uint(abs(x))>>{k});')
source.append('}');(a.output/'probe.comp').write_text('\n'.join(source)+'\n')
initial=[];expected=[]
for u in values:
 x=u if u<2**31 else u-2**32
 initial.extend([u]+[0xdeadbeef]*(stride-1))
 expected.append(u)
 for c in range(4):
  byte=(u>>(8*c))&255;expected.append((byte if byte<128 else byte-256)&0xffffffff)
 for variant in range(3):expected.extend([((1 if x>0 else -1 if x<0 else 0)*(abs(x)>>k))&0xffffffff for k in range(32)])
for name,data in [('input',initial),('expected',expected)]: (a.output/(name+'.bin')).write_bytes(struct.pack('<'+'I'*len(data),*data))
cmd=['glslangValidator','-V',str(a.output/'probe.comp'),'-o',str(a.output/'probe.spv')];r=subprocess.run(cmd,capture_output=True,text=True);(a.output/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
m=dict(lanes=len(values),workgroups=len(values)//32,output_words=len(expected),gpu_executed=False,reference='Python arbitrary-precision signed extraction and division magnitude; modulo 2^32 output',sha256={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in a.output.iterdir() if f.is_file()});(a.output/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(m))
