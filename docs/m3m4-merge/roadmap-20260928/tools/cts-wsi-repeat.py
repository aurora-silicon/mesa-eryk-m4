#!/usr/bin/env python3
"""Repeat one WSI case in a private compositor, inside the M3 guard.

Retains each QPA and X11 window tree. Stops at first non-Pass; no CTS bounds,
capabilities or results are changed. A passing retry does not erase a failure.
"""
import argparse,hashlib,json,os,shutil,subprocess,time
from pathlib import Path
from cts_headless import headless
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path);p.add_argument('--case',required=True)
p.add_argument('--prefix',required=True);p.add_argument('--count',type=int,default=20)
a=p.parse_args();assert 1<=a.count<=20 and a.case.startswith('dEQP-VK.wsi.')
state=json.loads((a.output/'result.json').read_text());assert state['phase']=='thermal-policy-ready' and state['watchdog']['armed'] and state['watchdog']['timeout_seconds']==60
assert shutil.which('xwininfo'), 'Install xwininfo before starting the diagnostic'
binary=Path('/root/cts-1.4.6.2/deqp-vk-seed30-scaled-blit')
env={k:v for k,v in os.environ.items() if not k.startswith(('VK_','MESA_','AGX_','HK_','ASAHI_','LD_','GALLIUM_','LIBGL_','__EGL_'))}
env.update(VK_DRIVER_FILES=a.prefix+'/asahi.json',LD_LIBRARY_PATH=a.prefix+'/lib',ASAHI_M3_EXPERIMENTAL='1',ASAHI_MESA_DEBUG='nocompress',HK_DEBUG_LOG='0',AGX_MESA_DEBUG='',VK_LAYER_PATH='/var/empty',VK_IMPLICIT_LAYER_PATH='/var/empty',MESA_SHADER_CACHE_DIR='/root/cts-regression-cache/mesa')
for k in ['DISPLAY','WAYLAND_DISPLAY','XAUTHORITY']:env.pop(k,None)
(a.output/'repeat-identity.json').write_text(json.dumps({str(p):hashlib.file_digest(p.open('rb'),'sha256').hexdigest() for p in [binary,Path(a.prefix)/'lib/libvulkan_asahi.so']},indent=2))
with headless(a.output,env) as display:
 for i in range(a.count):
  qpa=a.output/f'repeat-{i:02d}.qpa'
  with (a.output/f'repeat-{i:02d}.log').open('w') as log:
   child=subprocess.Popen([str(binary),'--deqp-case='+a.case,'--deqp-log-filename='+str(qpa),'--deqp-log-images=disable','--deqp-log-shader-sources=disable'],env=display,stdout=log,stderr=subprocess.STDOUT)
   try:
    deadline=time.monotonic()+15
    with (a.output/f'windows-{i:02d}.jsonl').open('w') as windows:
     while child.poll() is None:
      tree=subprocess.run(['xwininfo','-root','-tree'],env=display,text=True,capture_output=True,timeout=2)
      windows.write(json.dumps(dict(monotonic=time.monotonic(),tree=tree.stdout))+'\n');windows.flush()
      assert time.monotonic()<deadline,'Case exceeded diagnostic deadline'
      time.sleep(.05)
   finally:
    if child.poll() is None:child.kill()
    rc=child.wait(timeout=2)
  passed=rc==0 and '<Result StatusCode="Pass">' in qpa.read_text()
  print(json.dumps(dict(iteration=i,returncode=rc,passed=passed)),flush=True)
  if not passed:raise SystemExit(1)
print('WSI_REPEAT_PASS',flush=True)
