import hashlib,os,shutil,subprocess,sys
from pathlib import Path
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
shutil.copyfile(__file__,out/'runner.py')
binary='/root/vk-buffer-oracle-image-r1'
(out/'binaries.sha256').write_text(''.join(hashlib.sha256(Path(p).read_bytes()).hexdigest()+'  '+p+'\n' for p in [binary,'/root/vk-buffer-oracle-atomic64-final','/opt/asahi-m4-port/lib/libvulkan_asahi.so']))
subprocess.run(['python3','/root/run-shared-final-oracles.py',str(out/'buffer-shared')],check=True,timeout=200)
subprocess.run(['python3','/root/run-m4-gl.py','python3','/root/image-oracles-r3/run.py',str(out/'image'),binary],check=True,timeout=90)
print('PASS: 100 independent buffer shared image alias and scratch configurations',flush=True)
