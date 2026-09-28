import os,sys,subprocess
from pathlib import Path
chip,output=sys.argv[1:]
out=Path(output);out.mkdir(parents=True,exist_ok=False)
prefix='/opt/asahi-m3m4-allocation-20260928'
env=dict(os.environ,VK_DRIVER_FILES=prefix+'/asahi.json',LD_LIBRARY_PATH=prefix+'/lib',ASAHI_M3_EXPERIMENTAL='1',ASAHI_M4_EXPERIMENTAL='1',LD_PRELOAD='/root/asahi-map-failure.so')
for name,extra in [('command',{'ASAHI_TEST_MAP_SIZE':'131072'}),('shader',{'ASAHI_TEST_MAP_ALLOW_RETRY':'1'}),('shader-cold',{'MESA_SHADER_CACHE_DISABLE':'true'})]:
 subprocess.run(['/root/vk-command-map-failure',chip,str(out/(name+'.jsonl')),'map-failure'],env=dict(env,**extra),check=True)
print('ALL_MAP_BOUNDARIES_PASS')
