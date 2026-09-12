#!/usr/bin/env python3
"""Bounded standalone SDDM desktop probe under gpu-experiment.py.

A runtime systemd mount namespace supplies temporary autologin configuration.
Normal boot configuration stays intact. Restores SDDM and its remembered session
on exit; retains native scanout, maps and journal. Never an unattended desktop
stress runner. Requires an existing watchdog/thermal experiment guard.
"""
import argparse, hashlib, importlib.util, json, os, signal, subprocess, time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--guard-output',type=Path,required=True)
p.add_argument('--session',required=True,choices=['plasma','gnome'])
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
start=time.time();result=dict(session=a.session,boot=boot,passed=False,
    driver_sha256=hashlib.sha256(Path('/opt/asahi-m4-port/lib/libvulkan_asahi.so').read_bytes()).hexdigest())
try:
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
    assert '/opt/asahi-m4-port/lib/libvulkan_asahi.so' in maps,(name,'hardware driver not mapped')
 result['passed']=True
finally:
 # Remove the runtime override before restarting SDDM, even on failure.
 with (out/'journal.log').open('w') as log:
  subprocess.run(['journalctl','-b','--since','@'+str(int(start)),'--no-pager'],stdout=log,timeout=5)
 subprocess.run(['systemctl','stop','sddm','user@1000.service'],timeout=12)
 dropin.unlink(missing_ok=True);config.unlink(missing_ok=True)
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
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');os.sync()
 if fault:raise RuntimeError('GPU fault evidence saved; recover before restarting display manager')
print('PASS: standalone desktop observed; inspect renderer maps and screenshots')
