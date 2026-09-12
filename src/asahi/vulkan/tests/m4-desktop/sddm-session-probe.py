#!/usr/bin/env python3
"""Bounded standalone SDDM desktop probe under gpu-experiment.py.

A runtime systemd mount namespace supplies temporary autologin configuration.
Normal boot configuration stays intact. Restores SDDM and its remembered session
on exit; retains native scanout, maps and journal. Never an unattended desktop
stress runner. Requires an existing watchdog/thermal experiment guard.
"""
import argparse, hashlib, importlib.util, json, os, signal, subprocess, time, shutil, shlex
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--guard-output',type=Path,required=True)
p.add_argument('--session',required=True,choices=['plasma','gnome'])
p.add_argument('--renderer',choices=['asahi','lavapipe'],default='asahi')
p.add_argument('--capture',action='store_true')
p.add_argument('--egl-trace',type=Path,help='Optional forwarding-only EGL/GBM diagnostic preload')
p.add_argument('--seconds',type=int,default=10,choices=range(2,21))
a=p.parse_args()
spec=importlib.util.spec_from_file_location('fw',Path(__file__).with_name('firmware-snapshot.py'));fw=importlib.util.module_from_spec(spec);spec.loader.exec_module(fw)
boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
def check():fw.check_guard(a.guard_output,boot)
def run(cmd,timeout=8):check();return subprocess.run(cmd,check=True,timeout=timeout,text=True,capture_output=True)
def stop(signum,frame):raise InterruptedError(f'signal {signum}')
signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
check();out=a.guard_output/'desktop-session';out.mkdir()
state=Path('/var/lib/sddm/state.conf');previous=state.read_bytes() if state.exists() else None
main_config=Path('/etc/sddm.conf');main_existed=main_config.exists()
if previous is not None:(out/'sddm-state-before.conf').write_bytes(previous)
config=Path('/run/m4-sddm-session-probe.conf');dropin=Path('/run/systemd/system/sddm.service.d/90-m4-session-probe.conf')
assert not config.exists() and not dropin.exists()
entry=Path('/usr/share/wayland-sessions')/(a.session+'.desktop');assert entry.is_file()
(out/'session.desktop').write_bytes(entry.read_bytes())
required=['kwin_wayland','plasmashell'] if a.session=='plasma' else ['gnome-shell']
def procs():
 found={n:[] for n in required}
 for path in Path('/proc').glob('[0-9]*'):
  try:
   if path.stat().st_uid==1000:
    name=(path/'exe').resolve().name
    if name in found:found[name].append(path)
  except OSError:pass
 return found
runtime_env=Path('/run/systemd/user-environment-generators/99-m4-session-probe')
assert not runtime_env.exists()
scratch=Path('/home/e/.cache/m4-desktop-probes')/a.guard_output.name
scratch.mkdir(parents=True);os.chown(scratch,1000,1000)
(scratch/'compiler').mkdir();os.chown(scratch/'compiler',1000,1000)
start=time.time();result=dict(session=a.session,boot=boot,passed=False,
    renderer=a.renderer,
    driver_sha256=hashlib.sha256(Path('/opt/asahi-m4-port/lib/libvulkan_asahi.so').read_bytes()).hexdigest())
try:
 env={'VK_DRIVER_FILES':'/opt/asahi-m4-port/asahi_icd.json' if a.renderer=='asahi' else '/usr/share/vulkan/icd.d/lvp_icd.json',
      'LIBGL_ALWAYS_SOFTWARE':'0' if a.renderer=='asahi' else '1',
      'MESA_LOADER_DRIVER_OVERRIDE':'',
      'MESA_SHADER_CACHE_DIR':str(scratch/'mesa-cache'),'AGX_COMPILER_CAPTURE_DIR':str(scratch/'compiler')}
 if a.egl_trace:
  assert a.egl_trace.is_absolute() and a.egl_trace.is_file()
  env.update(LD_PRELOAD=str(a.egl_trace),EGL_LOG_LEVEL='debug')
  (out/'egl-trace.sha256').write_text(hashlib.sha256(a.egl_trace.read_bytes()).hexdigest()+'\n')
 if a.capture:
  env.update(VK_LAYER_PATH='/opt/gfxreconstruct/share/vulkan/explicit_layer.d',VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct',GFXRECON_CAPTURE_FILE=str(scratch/'session-${ProcessId}.gfxr'),GFXRECON_CAPTURE_FILE_TIMESTAMP='false',GFXRECON_CAPTURE_FILE_FLUSH='true')
 runtime_env.parent.mkdir(parents=True,exist_ok=True)
 # A generator avoids environment.d expanding GFXReconstruct's ${ProcessId}.
 lines=''.join(k+'='+shlex.quote(v)+'\n' for k,v in env.items())
 runtime_env.write_text('#!/bin/sh\nprintf %s '+shlex.quote(lines)+'\n')
 runtime_env.chmod(0o755)
 (out/'environment.json').write_text(json.dumps(env,indent=2)+'\n')
 config.write_text('[Autologin]\nUser=e\nSession='+a.session+'.desktop\nRelogin=false\n')
 dropin.parent.mkdir(parents=True,exist_ok=True)
 dropin.write_text('[Service]\nBindReadOnlyPaths='+str(config)+':/etc/sddm.conf\n')
 run(['systemctl','stop','sddm','user@1000.service'],12)
 run(['systemctl','daemon-reload'])
 run(['systemctl','start','sddm'],12)
 until=time.monotonic()+20
 while True:
  check();found=procs()
  if all(found.values()):break
  if time.monotonic()>until:raise RuntimeError('desktop processes did not start: '+str(found))
  time.sleep(.25)
 result['processes']={k:[int(p.name) for p in v] for k,v in found.items()}
 for stamp in [2,a.seconds]:
  until=time.monotonic()+stamp if stamp==2 else time.monotonic()+max(0,a.seconds-2)
  while time.monotonic()<until:check();time.sleep(.25)
  nodes={p.resolve() for p in Path('/sys/kernel/debug/dri').glob('*/m4_scanout')};assert len(nodes)==1
  (out/f'desktop-{stamp}.m4fb').write_bytes(nodes.pop().read_bytes())
  for name,paths in found.items():
   for path in paths:
    maps=(path/'maps').read_text();(out/f'{name}-{path.name}-maps.txt').write_text(maps)
    assert ('/opt/asahi-m4-port/lib/libvulkan_asahi.so' if a.renderer=='asahi' else '/usr/lib/libvulkan_lvp.so') in maps,(name,'selected driver not mapped')
 result['observed']=True
finally:
 # Remove the runtime override before restarting SDDM, even on failure.
 with (out/'journal.log').open('w') as log:
  subprocess.run(['journalctl','-b','--since','@'+str(int(start)),'--no-pager'],stdout=log,timeout=5)
 subprocess.run(['systemctl','stop','sddm','user@1000.service'],timeout=12)
 dropin.unlink(missing_ok=True);config.unlink(missing_ok=True);runtime_env.unlink(missing_ok=True)
 shutil.copytree(scratch,out/'diagnostics')
 if not main_existed and main_config.is_file() and main_config.stat().st_size==0:
  main_config.unlink()
 if previous is not None:state.write_bytes(previous)
 else:state.unlink(missing_ok=True)
 subprocess.run(['systemctl','daemon-reload'],check=True,timeout=8)
 journal=(out/'journal.log').read_text(errors='replace')
 fault=any(x in journal for x in ['GPU retirement unproven','userspace GPU timeout','firmware crashed','Kernel panic'])
 if not fault:
  subprocess.run(['systemctl','start','sddm'],check=True,timeout=12)
 else:
  result.update(passed=False,recovery_required=True)
 presentation_errors=[line for line in journal.splitlines() if any(x in line for x in ['Failed to lock front buffer','Failed to acquire front buffer','Error reported by eglSwapBuffers'])]
 result['presentation_errors']=presentation_errors
 result['passed']=result.get('observed',False) and not fault and not presentation_errors
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');os.sync()
 if fault:raise RuntimeError('GPU fault evidence saved; recover before restarting display manager')
assert result['passed'], 'Desktop presentation failed; see retained journal'
print('PASS: standalone desktop observed; inspect renderer maps and screenshots')
