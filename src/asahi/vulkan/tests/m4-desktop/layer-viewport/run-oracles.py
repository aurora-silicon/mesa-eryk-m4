import subprocess,sys,hashlib,shutil
from pathlib import Path
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
binary='/root/shader-layer-viewport-arm64'
shutil.copyfile(__file__,out/'runner.py')
(out/'binaries.sha256').write_text(''.join(hashlib.sha256(Path(p).read_bytes()).hexdigest()+'  '+p+'\n' for p in [binary,'/opt/asahi-m4-port/lib/libvulkan_asahi.so']))
failed=0
for stage in ['vertex','tess']:
 for mode in ['layer','viewport','both']:
  p=subprocess.run([binary,stage,mode],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15)
  (out/(stage+'-'+mode+'.log')).write_text(p.stdout);print(p.stdout,flush=True);failed+=p.returncode!=0
print(('FAIL' if failed else 'PASS')+': six layer viewport configurations',flush=True)
sys.exit(bool(failed))
