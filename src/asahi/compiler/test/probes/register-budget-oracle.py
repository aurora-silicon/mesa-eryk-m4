#!/usr/bin/env python3
"""Generate exact integer/barrier tests for 64..1024-thread register-heavy groups.
Use the generated run.py under a hardware watchdog/thermal guard. All buffer
bytes, including untouched input and padding, are checked against a CPU oracle.
"""
import argparse,json,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path)
a=p.parse_args();a.output.mkdir(exist_ok=False);cases=[]
for local in [64,256,512,1024]:
 for live in [32,80]:
  name=f'local{local}-live{live}';threads=local*2
  initial=bytearray([0xa5]*(4*(threads*live+threads+1024)))
  values=[]
  for i in range(threads*live):
   v=((i*0x9e3779b9)^((i+31)*0x3456789))&0xffffffff
   values.append(v);struct.pack_into('<I',initial,4*i,v)
  expected=bytearray(initial)
  for t in range(threads):
   neighbor=(t//local)*local+(t+17)%local
   acc=values[neighbor*live]
   for i in range(live):
    acc=(acc+((values[t*live+i]^values[t*live+(i*37+11)%live])*(2*i+1)))&0xffffffff
   struct.pack_into('<I',expected,4*(threads*live+t),acc)
  code=['#version 450',f'layout(local_size_x={local}) in;',
   'layout(set=0,binding=0,std430) coherent volatile buffer Data { uint v[]; } d;',
   f'shared uint exchange[{local}];',
   'void main(){uint t=gl_GlobalInvocationID.x;uint l=gl_LocalInvocationID.x;']
  code += [f'uint v{i}=d.v[t*{live}u+{i}u];' for i in range(live)]
  code += ['exchange[l]=v0;memoryBarrierBuffer();barrier();',f'uint acc=exchange[(l+17u)%{local}u];']
  code += [f'acc+=(v{i}^v{(i*37+11)%live})*{2*i+1}u;' for i in range(live)]
  code += [f'd.v[{threads*live}u+t]=acc;','}']
  src=a.output/(name+'.comp');src.write_text('\n'.join(code)+'\n')
  (a.output/(name+'.input')).write_bytes(initial);(a.output/(name+'.expected')).write_bytes(expected)
  subprocess.run(['glslangValidator','-V',str(src),'-o',str(a.output/(name+'.spv'))],check=True,stdout=subprocess.DEVNULL)
  cases.append(name)
(a.output/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
(a.output/'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;o=Path(sys.argv[1]);o.mkdir(parents=True,exist_ok=True)
for n in json.loads((r/'cases.json').read_text()):
 with (o/(n+'.log')).open('w') as f:
  p=subprocess.run([sys.argv[2],str(r/(n+'.spv')),str(r/(n+'.input')),str(r/(n+'.expected')),'2',str(o/(n+'.bin'))],stdout=f,stderr=subprocess.STDOUT,timeout=15)
 if p.returncode:print((o/(n+'.log')).read_text(),flush=True);raise SystemExit(p.returncode)
 assert (o/(n+'.bin')).read_bytes()==(r/(n+'.expected')).read_bytes()
 print('PASS:',n,flush=True)
print('PASS: 8 register-heavy barrier cases, 64..1024 threads, exact full buffers',flush=True)
''')
