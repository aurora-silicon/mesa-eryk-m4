#!/usr/bin/env python3
"""Guest-only bounded Steam launch of owned Factorio, under gpu-experiment.py.

Retains progress, process metadata and Factorio's own log in shared home.
Stops a newly launched game on failure. On successful startup it leaves the
ordinary application at its main menu for the next interactive qualification.
"""
import argparse,json,os,signal,subprocess,time,sys,shlex
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ipc',action='store_true')
parser.add_argument('--observe-only',action='store_true')
parser.add_argument('--seconds',type=int,default=85)
args=parser.parse_args()
assert 1<=args.seconds<=300
assert os.getuid()!=0 and os.sysconf('SC_PAGE_SIZE')==4096
out=Path.home()/'.cache/m4-factorio';out.mkdir(exist_ok=True)
def games():
 result={}
 for p in Path('/proc').glob('[0-9]*'):
  try:
   c=(p/'cmdline').read_bytes()
   if b'/Factorio/bin/x64/factorio' in c:
    result[int(p.name)]=c.replace(b'\0',b' ').decode(errors='replace')
  except OSError:pass
 return result
old=games();assert args.observe_only or not old, 'Factorio already running; do not start another copy'
log=Path.home()/'.factorio/factorio-current.log';start=time.time();success=False
try:
 if args.observe_only:
  pass
 elif args.ipc:
  pid=(Path.home()/'.steam/steam.pid').read_text().strip();assert pid.isdecimal()
  proc=Path('/proc')/pid;assert (proc/'comm').read_text().strip()=='steam'
  env=dict(v.split('=',1) for v in (proc/'environ').read_text().split('\0') if '=' in v)
  # Use the same FEX IPC socket and official launcher as the running client.
  command=[str(Path.home()/'.local/share/fex-steam/steam-launcher/bin_steam.sh'),'-applaunch','427520']
  subprocess.run(['/usr/bin/FEXBash','-c',shlex.join(command)],env=env,check=True,timeout=15)
 else:
  subprocess.run(['python3',str(Path.home()/'.cache/steam-cdp.py'),'--timeout','10','--expression','SteamClient.Apps.RunGame("427520","",-1,0)'],check=True,timeout=12)
 for i in range(args.seconds):
  procs=games()
  if procs:
   (out/'processes.json').write_text(json.dumps(procs,indent=2))
   for pid in procs:
    try:
     proc=Path('/proc')/str(pid)
     if (proc/'comm').read_text().strip()!='factorio':continue
     (out/f'factorio-{pid}.maps').write_bytes((proc/'maps').read_bytes())
     env={k:v for k,v in (e.split('=',1) for e in (proc/'environ').read_text().split('\0') if '=' in e) if k.startswith(('VK_','GFXRECON_','FEX_','LD_','MESA_','GALLIUM_'))}
     (out/f'factorio-{pid}.graphics-env.json').write_text(json.dumps(env,indent=2))
    except OSError:pass
  if log.exists() and (args.observe_only or log.stat().st_mtime>=start):
   text=log.read_text(errors='replace');(out/'factorio-current.log').write_text(text)
   if 'Factorio initialised' in text and procs:
    time.sleep(3)
    assert games(), 'Factorio exited immediately after startup'
    success=True;print('FACTORIO_INITIALISED',json.dumps(procs),flush=True);break
   if any(s in text for s in ('Error CrashHandler','Segmentation fault','Failed to create OpenGL')):
    raise RuntimeError(text[-4000:])
  if i%5==0:print('Factorio launch progress',i,sorted(procs),flush=True)
  time.sleep(1)
 if not success:raise RuntimeError('Factorio did not reach initialised state within launch bound')
finally:
 if not success:
  for pid in games().keys()-old.keys():
   try:os.kill(pid,signal.SIGTERM)
   except ProcessLookupError:pass
