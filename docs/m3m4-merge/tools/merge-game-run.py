#!/usr/bin/env python3
"""Run one M3 merge game probe under its watchdog guard and retain host logs."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path);p.add_argument('backend',choices=['gl','vulkan']);p.add_argument('selection',choices=['baseline','candidate']);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
remote='/root/m3-apps/'+a.output.name
cmd=['python3','/root/m3-gpu-tools/gpu/guard.py','--seconds','43','--keep-cpu-clocks',remote,'--','python3','/root/mesa-merge-probes/0ad-benchmark.py',remote,a.backend,'--environment=/root/mesa-merge-probes/m3-live-game-'+a.selection+'-environment.json','--prefetch','--cpu-performance','--uncapped','--no-gpu-profiler','--ten-seconds']
if a.backend=='vulkan':cmd+=['--traditional-descriptors']
(a.output/'command.json').write_text(json.dumps(cmd,indent=2))
result={'guard_returncode':None,'fetch_returncode':None}
with (a.output/'host-kernel-live.log').open('w') as klog,(a.output/'host-guard.log').open('w') as log:
 stream=subprocess.Popen(['ssh','-o','ConnectTimeout=10','root@fedora-m3','dmesg --follow-new --color=never'],stdout=klog,stderr=subprocess.STDOUT)
 try:
  result['guard_returncode']=subprocess.run(['ssh','-o','ConnectTimeout=15','-o','ServerAliveInterval=5','-o','ServerAliveCountMax=6','root@fedora-m3',*cmd],stdout=log,stderr=subprocess.STDOUT,timeout=85).returncode
 except subprocess.TimeoutExpired:result['error']='SSH timeout'
 finally:stream.terminate();stream.wait(timeout=5)
try:result['fetch_returncode']=subprocess.run(['scp','-r','-o','ConnectTimeout=15','root@fedora-m3:'+remote+'/.',str(a.output)],timeout=45).returncode
except subprocess.TimeoutExpired:result['fetch_error']='SSH timeout'
(a.output/'transport.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result),flush=True)
assert result['guard_returncode']==0 and result['fetch_returncode']==0
state=json.loads((a.output/'result.json').read_text())
assert state['phase']=='complete' and state['watchdog']['armed'] and state['watchdog']['timeout_seconds']==60
print((a.output/'frame-summary.json').read_text(),flush=True)
