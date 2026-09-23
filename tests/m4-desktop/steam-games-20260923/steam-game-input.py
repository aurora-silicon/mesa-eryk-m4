#!/usr/bin/env python3
"""Inject compositor-level input under an existing hardware experiment guard.

Target M4 only. Avoids XTEST requests unsupported by the microVM X11 proxy.
Coordinates are compositor logical pixels. Actions: move X Y, click, key NAME.
"""
import argparse,json,subprocess,time
p=argparse.ArgumentParser();p.add_argument('action',choices=['move','click','key']);p.add_argument('values',nargs='*');a=p.parse_args()
status=json.loads(subprocess.check_output(['python3','/root/ssh-watchdog.py','status']))
assert status['mode']=='experiment' and status['armed'] and status['timeout_seconds']==60
if a.action=='move':
 assert len(a.values)==2
 expr='hl.dispatch(hl.dsp.cursor.move({x=%d,y=%d}))'%tuple(map(int,a.values))
else:
 key='mouse:272' if a.action=='click' else a.values[0]
 expr='hl.dispatch(hl.dsp.send_shortcut({mods="",key='+json.dumps(key)+',window="activewindow"}))'
r=subprocess.run(['python3','/root/steam-desktop-command.py','hyprctl','eval',expr],capture_output=True,text=True,timeout=5)
print(r.stdout,r.stderr,flush=True);assert r.returncode==0 and r.stdout.strip()=='ok'
