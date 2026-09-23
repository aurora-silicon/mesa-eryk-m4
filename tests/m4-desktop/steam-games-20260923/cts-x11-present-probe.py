#!/usr/bin/env python3
"""Small guarded X11 present-wait integration check, M4 only."""
import os,sys,json,subprocess
from pathlib import Path
out=Path(sys.argv[1]);prefix=Path(sys.argv[2]);s=json.loads((out/'start.json').read_text());assert s['watchdog']['armed'] and s['watchdog']['timeout_seconds']==60
assert 'guarded-command.py' in Path('/proc',str(os.getppid()),'cmdline').read_text()
desktop=next(p for p in Path('/proc').glob('[0-9]*') if (p/'comm').exists() and (p/'comm').read_text().strip()=='Hyprland')
env=dict(v.split('=',1) for v in (desktop/'environ').read_text().split('\0') if '=' in v)
env.update(DISPLAY=':0',XDG_RUNTIME_DIR='/run/user/1000',VK_DRIVER_FILES=str(prefix/'asahi.json'),VK_ICD_FILENAMES=str(prefix/'asahi.json'),LD_LIBRARY_PATH=str(prefix/'lib'),ASAHI_M4_EXPERIMENTAL='1',HK_DEBUG_LOG='0')
cmd=['/root/cts-1.4.6.2/deqp-vk','--deqp-case=dEQP-VK.wsi.xlib.present_id_wait.wait.*','--deqp-log-filename='+str(out/'present.qpa'),'--deqp-log-images=disable']
auth=['python3','/root/steam-desktop-command.py','xhost']
subprocess.run(auth+['+SI:localuser:root'],check=True,timeout=5)
try:
 with (out/'cts.log').open('w') as log:subprocess.run(cmd,cwd='/root/cts-1.4.6.2',env=env,stdout=log,stderr=log,check=True,timeout=90)
finally:subprocess.run(auth+['-SI:localuser:root'],check=True,timeout=5)
s=(out/'cts.log').read_text();print(s,flush=True)
assert 'Passed:        6/6' in s or (s.count('(Pass)')==6 and '(Fail)' not in s)
print('X11_PRESENT_WAIT_CTS_COMPLETE',flush=True)
