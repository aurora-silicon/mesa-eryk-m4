#!/usr/bin/env python3
"""Stop the live display session under a guard, then wait for DRM clients to exit.

For a guarded module deployment/reboot, not a persistent text-only boot setting.
Usage on M4: python3 idle-display-session.py GUARD_OUTPUT [--uid 1000]
"""
from pathlib import Path
import argparse,json,subprocess,time
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path);p.add_argument('--uid',type=int,default=1000)
a=p.parse_args()
receipt=a.output/'start.json'
if not receipt.exists():receipt=a.output/'result.json'
guard=json.loads(receipt.read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds']==60
subprocess.run(['systemctl','stop','display-manager',f'user@{a.uid}.service'],check=True,timeout=20)
deadline=time.monotonic()+10
while True:
    r=subprocess.run(['fuser','/dev/dri/renderD128','/dev/dri/card1'],capture_output=True,text=True,timeout=2)
    with (a.output/'gpu-clients.jsonl').open('a') as log:
        log.write(json.dumps(dict(monotonic=time.monotonic(),returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))+'\n')
    if r.returncode==1: break
    if r.returncode!=0 or time.monotonic()>=deadline:
        raise RuntimeError(f'GPU clients did not exit: {r}')
    time.sleep(.25)
print('IDLE_REBOOT_READY',flush=True)
