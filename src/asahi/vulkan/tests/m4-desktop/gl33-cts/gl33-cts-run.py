#!/usr/bin/env python3
"""Host: run a pinned GL33 must-pass list through the existing guarded CTS runner.
The list is frozen in build/gl33-zink-20260921/catalog.json. This is the public
GL33 group on one EGL config, not a complete Khronos conformance submission.
"""
import argparse,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cases',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--name',required=True)
p.add_argument('--seconds',type=int,default=1500)
p.add_argument('--renderer',choices=['m4','lavapipe'],default='m4')
p.add_argument('--jobs',type=int,default=1)
p.add_argument('--config',choices=['64','113','fbo-width','fbo-height'],default='64')
p.add_argument('--environment',type=Path,required=True)
a=p.parse_args()
env=json.loads(a.environment.read_text())
if a.renderer=='lavapipe':
 env.update(LIBGL_ALWAYS_SOFTWARE='1',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/lvp_icd.json',VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/lvp_icd.json')
args=[sys.executable,str(root/'tools/cts-bulk.py'),'--cases',str(a.cases),'--output',str(a.output),'--name',a.name,
 '--renderer',a.renderer,'--runner',str(root/'reference/deqp-runner/target/aarch64-unknown-linux-gnu/release/deqp-runner'),
 '--binary','/root/gl-cts-1.4.6.2/glcts','--jobs',str(a.jobs),'--headless','--tests-per-group','50',
 '--runner-seconds',str(a.seconds),'--case-timeout','60','--cpu-max-khz','4104000']
config={'64':(64,64,1,'window'),'113':(113,47,2,'window'),'fbo-width':(64,-1,3,'fbo'),'fbo-height':(-1,64,3,'fbo')}[a.config]
w,h,seed,surface=config
options=['--deqp-log-flush=enable','--deqp-gl-context-type=egl','--deqp-egl-display-type=Wayland',f'--deqp-surface-type={surface}',f'--deqp-surface-width={w}',f'--deqp-surface-height={h}',f'--deqp-base-seed={seed}']
if surface=='fbo': options += ['--deqp-gl-config-name=rgba8888d24s8']
for arg in options:
 args+=['--deqp-arg='+arg]
for k,v in env.items():
 if k.startswith(('GALLIUM','MESA','VK_','__EGL','LD_LIBRARY','LIBGL','ASAHI','AGX_','HK_')):
  args+=['--env',k+'='+v]
  if k.startswith('AGX_'): args+=['--headless-env',k+'='+v]
raise SystemExit(subprocess.run(args,cwd=root).returncode)
