import os,subprocess,sys,shutil,hashlib
from pathlib import Path
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
shutil.copyfile(__file__,out/'runner.py')
binary='/root/vk-buffer-oracle-strict-buffer'
(out/'binaries.sha256').write_text(''.join(hashlib.sha256(Path(p).read_bytes()).hexdigest()+'  '+p+'\n' for p in [binary,'/opt/asahi-m4-port/lib/libvulkan_asahi.so']))
for name,path,env in [('image','/root/strict-image-oracles',{}),('image-wait','/root/strict-image-oracles',{'M4_ORACLE_SPARSE_WAIT':'1'}),('buffer','/root/strict-buffer-oracles',{})]:
 subprocess.run(['python3','/root/run-m4-gl.py','python3',path+'/run.py',str(out/name),binary],env=dict(os.environ,**env),check=True,timeout=35)
subprocess.run(['python3','/root/run-default-oracles.py',str(out/'sampler')],check=True,timeout=140)
print('PASS: strict 88 and sampler 207 independent configurations',flush=True)
