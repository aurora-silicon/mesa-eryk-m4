import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
failed=0
for c in json.loads((r/'cases.json').read_text()):
 name=c['name'];print('FORMATLESS64_CASE:',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(name+'.spv')),str(r/(name+'-input.bin')),str(r/(name+'-expected.bin')),'4',str(out/(name+'.bin'))],env=dict(os.environ,**c['env']),stdout=log,stderr=subprocess.STDOUT,timeout=30)
 print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True);failed+=p.returncode!=0
print(('FAIL' if failed else 'PASS')+': eight formatless integer64 image and buffer configurations',flush=True)
sys.exit(bool(failed))
