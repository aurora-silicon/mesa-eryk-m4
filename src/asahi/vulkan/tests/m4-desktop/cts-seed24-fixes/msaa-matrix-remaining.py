import json,os,subprocess,sys,time
from pathlib import Path
out=Path(sys.argv[1]); results=[]
for fmt in ['rg16f','rgba16f','rgba32f','rgba8i','rgba8ui','r32i']:
 for samples in [2,4]:
  start=time.monotonic()
  with (out/f'msaa-{fmt}-{samples}.log').open('w') as log:
   p=subprocess.run(['python3','/root/run-m4-gl.py','/root/multisample-probe-seed24',str(samples),fmt],stdout=log,stderr=subprocess.STDOUT,timeout=20,env=dict(os.environ,AGX_MESA_DEBUG='',HK_G17P_LAUNCH_TRACE='0'))
  text=(out/f'msaa-{fmt}-{samples}.log').read_text()
  passed=p.returncode==0 and 'PASS: multisample coverage resolve and reload' in text and 'FAIL' not in text
  results.append(dict(format=fmt,samples=samples,returncode=p.returncode,passed=passed,seconds=time.monotonic()-start))
  (out/'msaa-matrix.json').write_text(json.dumps(results,indent=2))
  print(results[-1],flush=True)
  if not passed:sys.exit(1)
print('PASS: all multisample format oracles',flush=True)
