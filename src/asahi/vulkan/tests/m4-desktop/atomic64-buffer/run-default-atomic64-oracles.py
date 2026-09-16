import hashlib,os,shutil,subprocess,sys
from pathlib import Path
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
shutil.copyfile(__file__,out/'runner.py')
binary='/root/vk-buffer-oracle-atomic64-final'
(out/'binaries.sha256').write_text(''.join(hashlib.sha256(Path(p).read_bytes()).hexdigest()+'  '+p+'\n' for p in [binary,'/opt/asahi-m4-port/lib/libvulkan_asahi.so']))
for name,path in [('operations','atomic64-buffer-oracles-r1'),('counter','native-counter-oracles-r1'),('alias-counter','native-alias-counter-oracles-r1'),('alias-pages','alias-page-oracles-r1'),('minmax','atomic64-oracles'),('prototype','atomic64-alias-lock-oracles-r2')]:
 subprocess.run(['python3','/root/run-m4-gl.py','python3','/root/'+path+'/run.py',str(out/name),binary],check=True,timeout=35)
print('PASS: 59 independent buffer atomic64 and alias configurations',flush=True)
