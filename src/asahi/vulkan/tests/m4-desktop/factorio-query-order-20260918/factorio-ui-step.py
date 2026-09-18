#!/usr/bin/env python3
"""Target root: focus Factorio and send one bounded XTEST action under the guard."""
import argparse,json,subprocess,time
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('action',choices=['key','click'])
p.add_argument('values',nargs='+')
a=p.parse_args()
def desktop(*args):subprocess.run(['python3','/root/steam-desktop-command.py',*args],check=True,timeout=10)
desktop('hyprctl','dispatch','hl.dsp.focus({window="class:factorio"})')
time.sleep(.3)
if a.action=='click':
 assert len(a.values)==2
 desktop('python3','/home/e/.cache/x11-input.py','move',*a.values)
 time.sleep(.3)
 desktop('python3','/home/e/.cache/x11-input.py','click','1')
else:
 assert len(a.values)==1
 desktop('hyprctl','dispatch','hl.dsp.send_shortcut({mods="",key='+json.dumps(a.values[0])+',window="class:factorio"})')
time.sleep(3)
print('FACTORIO_UI_STEP',flush=True)
