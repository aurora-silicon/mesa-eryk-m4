#!/usr/bin/env python3
"""Offscreen M3 CTS segment; run only as a child of the existing M3 guard.

At most 10 seconds launching tests, 10 seconds per case, one process at a time.
Keep every selected result; stop on device loss, process fault or kernel fault.
"""
import argparse, hashlib, json, os, re, resource, subprocess, time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path);p.add_argument('--start',type=int,required=True)
p.add_argument('--count',type=int,default=1000)
p.add_argument('--icd',type=Path,default=Path('/opt/asahi-m3-wayland/share/vulkan/icd.d/asahi_icd.armv8-a.json'))
a=p.parse_args()
library=Path(json.loads(a.icd.read_text())['ICD']['library_path'])
assert library.is_absolute() and library.is_file()
base=Path(os.environ.get('M3_CTS_BASE','/root/m3-cts'));binary=Path(os.environ.get('M3_CTS_BINARY','/root/cts-1.4.6.2/deqp-vk'))
assert 'guard.py' in Path('/proc',str(os.getppid()),'cmdline').read_bytes().decode()
assert json.loads((a.output/'result.json').read_text())['phase']=='thermal-policy-ready'
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
raw=(base/'cases.txt').read_bytes();meta=json.loads((base/'cases.meta.json').read_text())
assert hashlib.sha256(raw).hexdigest()==meta['cases_sha256']
cases=raw.decode().splitlines()
env=dict(os.environ,ASAHI_M3_EXPERIMENTAL='1',VK_DRIVER_FILES=str(a.icd),LD_LIBRARY_PATH=str(library.parent),HK_DEBUG_LOG='1',MESA_SHADER_CACHE_DISABLE='true',M3_TEST_UNCACHED='1',AGX_MESA_DEBUG='nopromote',HK_G17P_LAUNCH_TRACE='0')
for key in ('DISPLAY','WAYLAND_DISPLAY','XAUTHORITY'):env.pop(key,None)
boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
(a.output/'cts-identity.json').write_text(json.dumps(dict(boot_id=boot,environment={k:v for k,v in env.items() if k.startswith(('ASAHI','VK_','MESA_','M3_','AGX_','HK_','LD_'))},sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (binary,a.icd,library,Path(__file__))}),indent=2)+'\n')
before=subprocess.check_output(['dmesg','--color=never'],text=True)
case_seconds=int(os.environ.get('M3_CTS_CASE_SECONDS','10'));assert 1<=case_seconds<=35
launch_seconds=min(10,38-case_seconds)
results=[];start=time.monotonic();stop=None;fatal_kernel=None
def since(log, previous):
 if log.startswith(previous):return log[len(previous):]
 # dmesg ring wrap drops the oldest records. Select only timestamps newer
 # than the last saved message, never replay the boot log as new errors.
 stamps=re.findall(r'^\[\s*([0-9.]+)\]',previous,re.M)
 assert stamps, 'Cannot establish kernel log cursor'
 last=float(stamps[-1]);lines=[];keep=False
 for line in log.splitlines(True):
  match=re.match(r'^\[\s*([0-9.]+)\]',line)
  if match:keep=float(match.group(1))>last
  if keep:lines.append(line)
 return ''.join(lines)
previous_log=before
for i in range(a.start,min(len(cases),a.start+a.count)):
 if time.monotonic()-start>=launch_seconds:break
 directory=a.output/f'case-{i:04d}';directory.mkdir()
 qpa=directory/'result.qpa'
 argv=[str(binary),'--deqp-case='+cases[i],'--deqp-log-images='+os.environ.get('M3_CTS_LOG_IMAGES','disable'),'--deqp-log-shader-sources='+os.environ.get('M3_CTS_LOG_IMAGES','disable'),'--deqp-log-all-images='+os.environ.get('M3_CTS_LOG_IMAGES','disable'),'--deqp-visibility=hidden','--deqp-log-filename='+str(qpa)]
 r=dict(index=i,case=cases[i],catalog_index=meta['cases'][i]['catalog_index'],boot_id=boot,argv=argv)
 (directory/'case.json').write_text(json.dumps(r)+'\n')
 if os.environ.get('M3_CTS_CAPTURE_NIR') == '1':
  (directory/'nir').mkdir()
  env['HK_NIR_CAPTURE_DIR']=str(directory/'nir')
 started=time.monotonic();timed_out=False
 from contextlib import nullcontext
 from cts_headless import headless
 with headless(directory,env) if '.wsi.' in cases[i] else nullcontext(env) as case_env:
  with (directory/'stdout.log').open('w') as log:
   proc=subprocess.Popen(argv,cwd=binary.parent,env=case_env,stdout=log,stderr=subprocess.STDOUT)
   try:proc.wait(timeout=case_seconds)
   except subprocess.TimeoutExpired:
    timed_out=True;proc.kill();proc.wait(timeout=2)
 output=(directory/'stdout.log').read_text(errors='replace')
 content=qpa.read_text(errors='replace') if qpa.exists() else ''
 codes=re.findall(r'<Result StatusCode="([^"]+)"[^>]*>(.*?)</Result>',content,re.S)
 status,message=codes[-1] if codes else ('HarnessError','No completed CTS result')
 if timed_out:status,message='Timeout',f'{case_seconds}-second per-case bound exceeded'
 elif proc.returncode<0:status,message='Crash',f'Process signal {-proc.returncode}'
 elif status=='Pass' and proc.returncode:status,message='HarnessError','Pass with nonzero exit'
 r.update(status=status,message=message,returncode=proc.returncode,seconds=time.monotonic()-started)
 results.append(r);(directory/'case.json').write_text(json.dumps(r,indent=2)+'\n')
 (a.output/'cts-results.json').write_text(json.dumps(results,indent=2)+'\n')
 print(json.dumps(r),flush=True)
 after=subprocess.check_output(['dmesg','--color=never'],text=True)
 delta=since(after,before)
 (a.output/'cts-kernel-delta.log').write_text(delta)
 case_delta=since(after,previous_log)
 previous_log=after
 (directory/'kernel.log').write_text(case_delta)
 fatal_kernel=re.search(r'BUG:|Oops:|Kernel panic|SError|firmware crashed|co-processor has crashed|firmware error event|execution failed|asahi .*M3.*(?:timeout|failed)',case_delta,re.I)
 lines=case_delta.splitlines()
 admission_rejected=bool(re.search(r'IOCTL: submit failed! .*err: ENOTSUPP',case_delta)) and not fatal_kernel
 known_assert=(status=='Crash' and proc.returncode==-6 and not lines and
               "Assertion `vk_device_supports_stage(device, stage)' failed." in output)
 if known_assert:r['diagnosis']='Reviewed Mesa pipeline-stage assertion before hardware submission'
 if admission_rejected:
  r['diagnosis']='Kernel rejected unsupported command before scheduling; CTS outcome preserved'
  (directory/'case.json').write_text(json.dumps(r,indent=2)+'\n')
  (a.output/'cts-results.json').write_text(json.dumps(results,indent=2)+'\n')
 # Ordinary CTS Fail/NotSupported are outcomes, not guard failures. A device
 # loss requires reviewing the retained logs before any subsequent segment.
 (directory/'case.json').write_text(json.dumps(r,indent=2)+'\n')
 (a.output/'cts-results.json').write_text(json.dumps(results,indent=2)+'\n')
 # Current M3 runtime reuses separate compute/render graphs across processes.
 # Completed GPU work no longer requires the early bring-up reboot protocol.
 if fatal_kernel or (status in ('Crash','Timeout','HarnessError') and not known_assert) or ('VK_ERROR_DEVICE_LOST' in output+content and not admission_rejected):
  stop='review-required';break
(a.output/'cts-summary.json').write_text(json.dumps(dict(start=a.start,next_index=a.start+len(results),count=len(results),stop=stop,elapsed_seconds=time.monotonic()-start),indent=2)+'\n')
os.sync()
# Hardware faults retain watchdog recovery. User-mode rejections stop the
# segment but let the guard save logs and disarm for host-side review.
if fatal_kernel:raise RuntimeError('kernel fault; retain watchdog recovery')
