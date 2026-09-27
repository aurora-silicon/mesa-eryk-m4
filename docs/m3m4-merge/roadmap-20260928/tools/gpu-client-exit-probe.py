#!/usr/bin/env python3
"""Guarded native Vulkan queued-work exit/SIGKILL and allocation-lifetime check.

Requires finite verified compute inputs in /root/roadmap-lifetime-probe.
The oracle's one-nanosecond fence wait must return VK_TIMEOUT before exit.
This proves queued or executing work, not a particular engine phase.
"""
from pathlib import Path
import argparse,json,os,subprocess,time,hashlib
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path)
a=p.parse_args();out=a.output
receipt=out/'start.json'
if not receipt.exists():receipt=out/'result.json'
state=json.loads(receipt.read_text());assert state['watchdog']['armed'] and state['watchdog']['timeout_seconds']==60
base=Path('/root/roadmap-lifetime-probe');memory_files=list(Path('/sys/kernel/debug').glob('asahi-*/memory'))
assert len(memory_files)==1,memory_files
memory_path=memory_files[0]
env={k:v for k,v in os.environ.items() if not k.startswith(('VK_','MESA_','AGX_','HK_','ASAHI_','LD_','M4_ORACLE_'))}
settings=dict(VK_DRIVER_FILES='/opt/asahi-m3m4-cts-20260927/asahi.json',VK_ICD_FILENAMES='/opt/asahi-m3m4-cts-20260927/asahi.json',LD_LIBRARY_PATH='/opt/asahi-m3m4-cts-20260927/lib',ASAHI_M3_EXPERIMENTAL='1',ASAHI_M4_EXPERIMENTAL='1',ASAHI_MESA_DEBUG='nocompress',HK_DEBUG_LOG='0',AGX_MESA_DEBUG='',VK_LAYER_PATH='/var/empty',VK_IMPLICIT_LAYER_PATH='/var/empty')
env.update(settings)
(out/'lifetime-environment.json').write_text(json.dumps(settings,indent=2)+'\n')
(out/'lifetime-inputs.json').write_text(json.dumps({f:hashlib.sha256((base/f).read_bytes()).hexdigest() for f in ['vk-buffer-oracle','probe.spv','input.bin','reference.bin']},indent=2)+'\n')
cmd=[str(base/'vk-buffer-oracle'),str(base/'probe.spv'),str(base/'input.bin'),str(base/'reference.bin'),'1',str(out/'actual.bin')]
rows=[];before=None;after=None

def memory():
    lines=memory_path.read_text().splitlines();assert lines[0]=='version=1 unit=allocation_extent_bytes'
    return {s[0]:{k:int(v) for k,v in (x.split('=') for x in s[1:])} for s in map(str.split,lines[1:])}

def run(mode):
    child_env=dict(env)
    if mode:child_env['M4_ORACLE_EXIT_INFLIGHT']=mode
    result=subprocess.run(cmd,env=child_env,capture_output=True,text=True,timeout=10)
    with (out/'clients.log').open('a') as log:log.write(f'MODE {mode}\n'+result.stdout+result.stderr)
    expected=-9 if mode=='sigkill' else 0
    assert result.returncode==expected,(mode,result.returncode,result.stderr)
    if mode:assert f'LIFETIME_PROBE pending_result=2 mode={mode}' in result.stderr
    rows.append(dict(mode=mode or 'checked_render',returncode=result.returncode,memory=memory()))
    (out/'clients.json').write_text(json.dumps(rows,indent=2)+'\n')

try:
    subprocess.run(['python3','/root/idle-display-session.py',str(out)],check=True,timeout=32)
    run(None) # Independent uint32 reference must pass before any exit experiment.
    for _ in range(2):run('exit');run('sigkill');run(None)
    time.sleep(.5);before=memory()
    for _ in range(12):run('exit');run('sigkill');run(None)
    time.sleep(.5);after=memory()
    for kind in before:
        for field in ['objects','bytes']:
            assert after[kind][field]==before[kind][field],(kind,field,before,after)
    print('CLIENT_EXIT_LIFETIME_PASS',flush=True)
finally:
    (out/'lifetime-memory.json').write_text(json.dumps(dict(before=before,after=after),indent=2)+'\n')
    subprocess.run(['systemctl','start','display-manager'],check=True,timeout=15)
