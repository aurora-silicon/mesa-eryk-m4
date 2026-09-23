#!/usr/bin/env python3
"""Inject compositor-level input under an existing hardware experiment guard.

Target M4 only. Avoids XTEST requests unsupported by the microVM X11 proxy.
Coordinates are compositor logical pixels. Actions: move X Y, click, key NAME.
"""
import argparse,json,subprocess,time
p=argparse.ArgumentParser();p.add_argument('action',choices=['move','click','key','point-click']);p.add_argument('values',nargs='*');p.add_argument('--hold',type=float,default=.6);a=p.parse_args();assert .05<=a.hold<=5
status=json.loads(subprocess.check_output(['python3','/root/ssh-watchdog.py','status']))
assert status['mode']=='experiment' and status['armed'] and status['timeout_seconds']==60
if a.action=='point-click':
 assert len(a.values)==4, 'X Y desktop-width desktop-height'
 subprocess.run(['python3','/root/steam-desktop-command.py','bench-pointer',*a.values,'click'],check=True,timeout=8)
 raise SystemExit(0)
if a.action=='move':
 assert len(a.values)==2
 expr='hl.dispatch(hl.dsp.cursor.move({x=%d,y=%d}))'%tuple(map(int,a.values))
else:
 key='mouse:272' if a.action=='click' else a.values[0]
 # An explicit window target resets pointer coordinates to (1,1).
 # Preserve the compositor pointer and hold long enough for a game tick.
 for state in ('down','up'):
  expr='hl.dispatch(hl.dsp.send_key_state({mods="",key='+json.dumps(key)+',state='+json.dumps(state)+'}))'
  r=subprocess.run(['python3','/root/steam-desktop-command.py','hyprctl','eval',expr],capture_output=True,text=True,timeout=5)
  print(r.stdout,r.stderr,flush=True);assert r.returncode==0 and r.stdout.strip()=='ok'
  if state=='down':time.sleep(max(a.hold,2) if key=='F9' else a.hold)
 raise SystemExit(0)
r=subprocess.run(['python3','/root/steam-desktop-command.py','hyprctl','eval',expr],capture_output=True,text=True,timeout=5)
print(r.stdout,r.stderr,flush=True);assert r.returncode==0 and r.stdout.strip()=='ok'
