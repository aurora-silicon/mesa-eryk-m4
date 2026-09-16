import hashlib, os, shutil, subprocess, sys
from pathlib import Path
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
assert 'HK_EXPERIMENTAL_MINMAX' not in os.environ
binary='/root/dx-texel-cases/vk-buffer-oracle-minmax'
shutil.copyfile(__file__,out/'runner.py')
with (out/'binaries.sha256').open('w') as f:
 for name in [binary,'/root/dx-query-minmax','/opt/asahi-m4-port/lib/libvulkan_asahi.so']:
  f.write(hashlib.sha256(Path(name).read_bytes()).hexdigest()+'  '+name+'\n')
suites=[('minmax','/root/minmax-oracles-v5'),('view','/root/dx-view-min-combinations-v2'),('weighted','/root/weighted-view-oracles-v2'),('shadow','/root/weighted-shadow-oracles'),('sparse','/root/minmax-sparse-oracles-v2')]
for name,path in suites:
 subprocess.run(['python3','/root/run-m4-gl.py','python3',path+'/run.py',str(out/name),binary],check=True,timeout=35)
for mode in [0,1]:
 for reduction in [1,2]:
  for nearest in [False,True]:
   env=dict(os.environ,M4_QUERY_REDUCTION=str(reduction),M4_QUERY_BIAS='.75',M4_QUERY_MIN='1.25',M4_QUERY_MAX='3.25')
   if nearest:env['M4_QUERY_NEAREST']='1'
   subprocess.run(['python3','/root/run-m4-gl.py','/root/dx-query-minmax',str(mode)],env=env,check=True,timeout=20)
print('PASS: default sampler 207 configurations',flush=True)
