#!/usr/bin/env python3
"""Host-side continuation of a frozen M3 CTS list in individually guarded segments.

Stops for review on any unexpected device loss, crash, timeout or guard failure.
Copies evidence after each segment before scheduling the next one.
"""
import argparse,collections,json,subprocess,time,shlex
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('--start',type=int,required=True);p.add_argument('--initialize',action='store_true');p.add_argument('--log-images',action='store_true');p.add_argument('--remote-base',default='/root/m3-cts');p.add_argument('--binary',default='/root/cts-1.4.6.2/deqp-vk');p.add_argument('--binary-sha256');p.add_argument('--icd',default='/opt/asahi-m3-wayland/share/vulkan/icd.d/asahi_icd.armv8-a.json');p.add_argument('--stop-on-nonpass',action='store_true');p.add_argument('--case-seconds',type=int,default=10,choices=range(1,36));p.add_argument('--env',action='append',default=[],metavar='KEY=VALUE');a=p.parse_args()
extra_env=dict(item.split('=',1) for item in a.env)
assert all(k.replace('_','').isalnum() and k[0].isalpha() for k in extra_env)
ssh=['ssh','-o','ConnectTimeout=10','root@fedora-m3']
a.output.mkdir(parents=True,exist_ok=True);index=a.start
case_count=len((a.output/"cases.txt").read_text().splitlines())
root=Path(__file__).resolve().parents[2]
# Pin the actual executable and installed driver before starting the guard.
icd=json.loads(subprocess.check_output(ssh+[shlex.join(['cat',a.icd])],text=True,timeout=20))
library=icd['ICD']['library_path']
assert Path(library).is_absolute()
paths=[a.binary, library, '/root/m3-gpu-tools/asahi.ko']
lines=subprocess.check_output(ssh+[shlex.join(['sha256sum', *paths])],text=True,timeout=30).splitlines()
sha256={line.split(None,1)[1].strip():line.split(None,1)[0] for line in lines}
assert set(sha256)==set(paths)
if a.binary_sha256:assert sha256[a.binary]==a.binary_sha256, 'CTS executable hash mismatch'
identity=dict(binary=a.binary,sha256=sha256,case_count=case_count,per_case_seconds=a.case_seconds,
              launch_window_seconds=min(10,38-a.case_seconds),guard_limit_seconds=45 if a.case_seconds>10 else 30,watchdog_seconds=60,
              log_images=a.log_images,remote_base=a.remote_base,icd=a.icd,stop_on_nonpass=a.stop_on_nonpass)
if extra_env:identity['extra_environment']=extra_env
identity_path=a.output/'execution.json'
if identity_path.exists():assert json.loads(identity_path.read_text())==identity, 'Run identity changed'
else:identity_path.write_text(json.dumps(identity,indent=2)+'\n')
def initialize(index, reboot=False):
 if reboot:
  old=subprocess.check_output(ssh+['cat /proc/sys/kernel/random/boot_id'],text=True).strip()
  subprocess.run(ssh+['sync; systemctl reboot'],timeout=15)
 else:old=None
 deadline=time.monotonic()+300
 while True:
  try:
   boot=subprocess.check_output(ssh+['cat /proc/sys/kernel/random/boot_id; systemctl is-active m3-dcp plasmalogin'],text=True,stderr=subprocess.DEVNULL,timeout=12).splitlines()
   if len(boot)==3 and boot[0]!=old and boot[1:]==['active','active']:break
  except (subprocess.SubprocessError,OSError):pass
  if time.monotonic()>deadline:raise RuntimeError('Boot readiness exceeded five minutes')
  time.sleep(10)
 print('Fresh boot ready: '+boot[0],flush=True)
 capture=f'cts-boot-{index:04d}';remote='/root/m3-gpu-evidence/'+capture
 with (a.output/(capture+'.log')).open('w') as log:
  subprocess.run(ssh+[f'python3 /root/m3-gpu-tools/collect.py --output {remote} --firmware'],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=45)
 subprocess.run(['scp','-q','-r','root@fedora-m3:'+remote,str(a.output)],check=True,timeout=30)
 overlay=a.output/(capture+'.dts')
 subprocess.run(['python3',str(root/'tools/m3-boot-export/make-gpu-overlay.py'),str(a.output/capture),str(root/'build/m3-live.dtb'),str(overlay)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 subprocess.run(['scp','-q',str(overlay.with_suffix('.dtbo')),'root@fedora-m3:/lib/firmware/m3-native-gpu.dtbo'],check=True,timeout=15)
 out=f'/root/m3-cts/native-init-{index:04d}'
 with (a.output/f'native-init-{index:04d}.log').open('w') as log:
  result=subprocess.run(ssh+[f'python3 /root/m3-gpu-tools/gpu/guard.py {out} -- python3 /root/m3-gpu-tools/gpu/native-start.py {remote} /root/m3-gpu-tools'],stdout=log,stderr=subprocess.STDOUT,timeout=45)
 subprocess.run(['scp','-q','-r','root@fedora-m3:'+out,str(a.output)],check=True,timeout=30)
 result.check_returncode()
if a.initialize:initialize(index)
while index<case_count:
 name=f'segment-{index:04d}';remote=a.remote_base+'/'+name
 command=' '.join(shlex.quote(k+'='+v) for k,v in extra_env.items())+' '+f'M3_CTS_CASE_SECONDS={a.case_seconds} M3_CTS_BINARY={shlex.quote(a.binary)} M3_CTS_LOG_IMAGES={"enable" if a.log_images else "disable"} M3_CTS_BASE={shlex.quote(a.remote_base)} python3 /root/m3-gpu-tools/gpu/guard.py --seconds {45 if a.case_seconds>10 else 30} --keep-cpu-clocks {shlex.quote(remote)} -- python3 /root/m3-gpu-tools/gpu/cts-batch.py {shlex.quote(remote)} --start {index} --icd {shlex.quote(a.icd)}'
 with (a.output/(name+'.log')).open('w') as log:
  result=subprocess.run(ssh+[command],stdout=log,stderr=subprocess.STDOUT,timeout=50)
 (a.output/name).mkdir(exist_ok=True)
 subprocess.run(['rsync','-az','root@fedora-m3:'+remote+'/',str(a.output/name)+'/'],check=True,timeout=30)
 summary=json.loads((a.output/name/'cts-summary.json').read_text())
 records=json.loads((a.output/name/'cts-results.json').read_text())
 observed=json.loads((a.output/name/'cts-identity.json').read_text())['sha256']
 assert observed[a.binary]==sha256[a.binary], 'CTS binary changed'
 assert observed[paths[1]]==sha256[paths[1]], 'Mesa changed'
 guard=json.loads((a.output/name/'result.json').read_text())
 assert guard['tool_sha256'][paths[2]]==sha256[paths[2]], 'Kernel module changed'
 print(json.dumps(dict(segment=name,**summary,counts=dict(collections.Counter(r['status'] for r in records)))),flush=True)
 if result.returncode or summary['stop'] not in (None,'reboot-after-gpu-execution'):raise SystemExit('Stopped for review; preserve recorded next_index')
 if a.stop_on_nonpass and any(r['status'] not in ('Pass','NotSupported') for r in records):raise SystemExit('Non-pass outcome; preserve and triage before continuing')
 assert summary['next_index']>index
 index=summary['next_index']
 if summary['stop']=='reboot-after-gpu-execution' and index<case_count:initialize(index,reboot=True)
