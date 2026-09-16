import hashlib,os,shutil,subprocess,sys
from pathlib import Path
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
shutil.copyfile(__file__,out/'runner.py')
binary=os.environ.get('ORACLE','/root/vk-buffer-oracle-atomic64-final')
base=Path(os.environ.get('CASES','/root'))
for name in ['tps-repeat-r1','tps-dynstore-r1','tps-spill-r1']:
 for spill in [False,True]:
  label=name+('-forced' if spill else '')
  r=base/name
  env=dict(os.environ, AGX_MESA_DEBUG="")
  if spill:env['AGX_MESA_DEBUG']='spill'
  with (out/(label+'.log')).open('w') as log:
   result=subprocess.run([binary,str(r/'probe.spv'),str(r/'input.bin'),str(r/'reference.bin'),'1',str(out/(label+'.bin'))],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=30)
  print(('PASS' if result.returncode==0 else 'FAIL')+': '+label,flush=True)
  if result.returncode:sys.exit(1)
print('PASS: six atomic-return scratch and spill configurations',flush=True)
