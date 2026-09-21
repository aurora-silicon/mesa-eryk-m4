#!/usr/bin/env python3
"""Open the retained, qualified Firefox profile for an attended YouTube demo.
Run on the target under gpu-experiment.py; GUARD_OUTPUT is the only argument.
The browser is handed to the user after playback verification. Failed setup
stops only this demo unit. The ordinary user profile is not modified.
"""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time

out=Path(sys.argv[1])
spec=importlib.util.spec_from_file_location('probe','/root/firefox-probe.py')
probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)
spec=importlib.util.spec_from_file_location('fw','/root/firmware-snapshot.py')
fw=importlib.util.module_from_spec(spec);spec.loader.exec_module(fw)
boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
fw.check_guard(out,boot)
profile='/home/e/.cache/m4-firefox-probes/20260921T165256936203Z-firefox-panel-1080-visible'
assert Path(profile,'user.js').is_file()
assert not Path(profile,'lock').is_symlink(), 'Retained demo profile already open'
unit='m4-firefox-youtube-demo'
base=['python3','/root/steam-desktop-command.py']
subprocess.run(base+['systemd-run','--user','--collect','--unit='+unit,'--property=Type=exec',
                    '/usr/bin/firefox','--no-remote','--new-instance','--marionette',
                    '--remote-allow-system-access','--profile',profile,'about:blank'],check=True)
remote=None
handed_over=False
try:
    until=time.monotonic()+15
    while True:
        fw.check_guard(out,boot)
        try:
            remote=probe.Marionette();break
        except ConnectionRefusedError:
            if time.monotonic()>until:raise
            time.sleep(.25)
    remote.call('WebDriver:NewSession',{'capabilities':{'alwaysMatch':{}}})
    remote.call('WebDriver:SetTimeouts',{'script':6000,'pageLoad':12000,'implicit':0})
    remote.call('WebDriver:Navigate',{'url':'https://www.youtube.com/watch?v=aqz-KE-bpKQ'})
    setup=remote.script('''
const element=document.getElementById('movie_player'),p=element?.wrappedJSObject||element;
if(!p || typeof p.setPlaybackQualityRange!=='function')throw Error('Player unavailable');
p.setPlaybackQualityRange('hd1080','hd1080');
const v=document.querySelector('video');if(!v)throw Error('No video');
v.muted=true;v.play();
return {time:v.currentTime};''')
    deadline=time.monotonic()+15
    while True:
        time.sleep(1);fw.check_guard(out,boot)
        state=remote.script('''const v=document.querySelector('video');v.muted=true;if(v.paused)v.play().catch(()=>{});return {
 time:v.currentTime,paused:v.paused,ready:v.readyState,width:v.videoWidth,height:v.videoHeight,
 visibility:document.visibilityState,muted:v.muted};''')
        if state['time']>setup['time']+3 and not state['paused'] and state['height']==1080 and state['visibility']=='visible':break
        if time.monotonic()>deadline:raise RuntimeError(state)
    (out/'youtube-demo.json').write_text(json.dumps(dict(unit=unit,profile=profile,playback=state),indent=2)+'\n')
    remote.call('WebDriver:DeleteSession')
    handed_over=True
    print('PASS: visible 1080p YouTube playing; browser handed to user (muted)',flush=True)
finally:
    if remote:remote.socket.close()
    if not handed_over:
        subprocess.run(base+['systemctl','--user','stop',unit],check=False)
