#!/usr/bin/env python3
"""Compare checked native and microVM GPU round trips inside gpu-experiment.

Uses identical ARM64 binaries/Mesa, without FEX or application input. The guest
probe is deliberately tiny; this measures submission/fence overhead, not FPS.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import statistics
import subprocess
import sys
import time

out = Path(sys.argv[2])
guest = sys.argv[1] == 'guest'
if not guest:
    guard = json.loads((out/'start.json').read_text())
    assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds'] == 60
    for p in Path('/proc').glob('[0-9]*/cmdline'):
        try:
            argv=p.read_bytes().split(b'\0')
            assert argv[0] != b'/usr/bin/muvm', 'Existing microVM; leave it alone'
        except OSError: pass
    shared = Path('/home/e/.local/share/factorio-driver')/('turnaround-'+out.name)
    shared.mkdir(mode=0o755)
    uid=1000;gid=1000
    os.chown(shared,uid,gid)
    for name,source in [('probe','/root/vk-priority-probe'),('load.spv','/root/load.spv'),('runner.py',__file__)]:
        shutil.copy2(source,shared/name);os.chown(shared/name,uid,gid)
    identities={name:hashlib.sha256((shared/name).read_bytes()).hexdigest() for name in ('probe','load.spv')}
    identities['mesa']=hashlib.sha256(Path('/opt/asahi-m4-port/lib/libvulkan_asahi.so').read_bytes()).hexdigest()
    (out/'identities.json').write_text(json.dumps(identities,indent=2))
else:
    shared=out
    assert shared.name.startswith('turnaround-')
    identities={'mesa':hashlib.sha256(Path('/opt/asahi-m4-port/lib/libvulkan_asahi.so').read_bytes()).hexdigest()}
    (shared/'guest-identities.json').write_text(json.dumps(identities))

env=dict(os.environ,ASAHI_M4_EXPERIMENTAL='1',VK_DRIVER_FILES='/opt/asahi-m4-port/asahi_icd.json',LD_LIBRARY_PATH='/opt/asahi-m4-port/lib',ASAHI_MESA_DEBUG='nocompress')
for batch in (1,16):
    name=('guest' if guest else 'host')+'-'+str(batch)
    go=shared/(name+'-go');go.touch()
    subprocess.run([str(shared/'probe'),str(shared/'load.spv'),'1','64',str(batch),'64',str(shared/(name+'-ready')),str(go),str(shared/(name+'.jsonl'))],env=env,check=True,timeout=12)
if guest:
    print('PASS: guest GPU round trips and all readbacks',flush=True)
    raise SystemExit(0)

command=['python3','/root/steam-desktop-command.py','/usr/bin/muvm','--','/usr/bin/env','VK_DRIVER_FILES=/opt/asahi-m4-port/asahi_icd.json','LD_LIBRARY_PATH=/opt/asahi-m4-port/lib','ASAHI_M4_EXPERIMENTAL=1','ASAHI_MESA_DEBUG=nocompress','/usr/bin/python3',str(shared/'runner.py'),'guest',str(shared)]
child=None
signal.signal(signal.SIGTERM,lambda *_: (_ for _ in ()).throw(InterruptedError('guard stopped')))
try:
    child=subprocess.Popen(command,start_new_session=True)
    child.wait(timeout=25)
    assert child.returncode==0,child.returncode
finally:
    if child is not None and child.poll() is None:
        os.killpg(child.pid,signal.SIGTERM)
        try:child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid,signal.SIGKILL);child.wait()
    for p in shared.iterdir():
        if p.suffix in ('.json','.jsonl'):shutil.copy2(p,out/p.name)
assert identities['mesa']==json.loads((shared/'guest-identities.json').read_text())['mesa']
summary={}
for p in out.glob('*-*.jsonl'):
    if p.name.startswith(('host-','guest-')):
        rows=[json.loads(line) for line in p.read_text().splitlines()][1:]
        summary[p.stem]={metric:{'median':statistics.median(vals),'p95':sorted(vals)[int(.95*(len(vals)-1))]} for metric,vals in [('round_trip_ms',[r['latency_ms'] for r in rows]),('submit_ms',[(r['submit_ns']-r['start_ns'])/1e6 for r in rows])]}
(out/'turnaround-summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary),flush=True)
print('PASS: native and microVM GPU round trips and all readbacks',flush=True)
