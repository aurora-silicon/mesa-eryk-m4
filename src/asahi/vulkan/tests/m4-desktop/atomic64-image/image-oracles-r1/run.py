import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
for c in json.loads((r/'cases.json').read_text()):
 name=c['name'];print('IMAGE64_CASE:',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(name+'.spv')),str(r/(name+'-input.bin')),str(r/(name+'-expected.bin')),'4',str(out/(name+'.bin'))],env=dict(os.environ,**c['env']),stdout=log,stderr=subprocess.STDOUT,timeout=30)
 print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True)
 if p.returncode:sys.exit(1)
print('PASS: twelve image64 operation and load-store configurations',flush=True)
