#!/usr/bin/env python3
"""Target root: focus Factorio and send one bounded XTEST action under the guard."""
import argparse,json,subprocess,time
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('action',choices=['key','click','capture'])
p.add_argument('values',nargs='*')
p.add_argument('--screenshot')
a=p.parse_args()
def desktop(*args):subprocess.run(['python3','/root/steam-desktop-command.py',*args],check=True,timeout=10)
clients=json.loads(subprocess.check_output(['python3','/root/steam-desktop-command.py','hyprctl','clients','-j'],text=True,timeout=10))
assert any(c.get('class')=='factorio' for c in clients),'Factorio window is not present'
desktop('hyprctl','dispatch','hl.dsp.focus({window="class:factorio"})')
time.sleep(.3)
if a.action=='click':
 assert len(a.values)==2
 desktop('python3','/home/e/.cache/x11-input.py','move',*a.values)
 time.sleep(.3)
 desktop('python3','/home/e/.cache/x11-input.py','click','1')
elif a.action=='key':
 assert len(a.values)==1
 desktop('hyprctl','dispatch','hl.dsp.send_shortcut({mods="",key='+json.dumps(a.values[0])+',window="class:factorio"})')
time.sleep(3)
if a.screenshot:
 with open(a.screenshot,'wb') as f:
  subprocess.run(['python3','/root/steam-desktop-command.py','grim','-o','eDP-1','-'],stdout=f,check=True,timeout=10)
print('FACTORIO_UI_STEP',flush=True)
