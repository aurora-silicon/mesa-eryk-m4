import hashlib,os,shutil,subprocess,sys
from pathlib import Path
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
shutil.copyfile(__file__,out/'runner.py')
binary='/root/vk-buffer-oracle-atomic64-final'
(out/'binaries.sha256').write_text(''.join(hashlib.sha256(Path(p).read_bytes()).hexdigest()+'  '+p+'\n' for p in [binary,'/opt/asahi-m4-port/lib/libvulkan_asahi.so']))
subprocess.run(['python3','/root/run-default-atomic64-oracles.py',str(out/'buffer')],check=True,timeout=100)
for name in ['shared-oracles-r2','shared-counter-oracles-r1']:
 subprocess.run(['python3','/root/run-m4-gl.py','python3','/root/'+name+'/run.py',str(out/name),binary],check=True,timeout=40)
subprocess.run(['python3','/root/run-m4-gl.py','python3','/root/run-shared-tps-oracles.py',str(out/'tps')],check=True,timeout=80)
print('PASS: 84 buffer shared alias and scratch configurations',flush=True)
