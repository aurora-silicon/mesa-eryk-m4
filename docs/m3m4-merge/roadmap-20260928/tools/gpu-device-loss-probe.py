#!/usr/bin/env python3
"""Deliberately overlong finite compute under an existing M3/M4 experiment guard.

Expected outcome: Vulkan DEVICE_LOST, retained first-fault evidence, guard
failure and watchdog recovery. Never classify this as a successful render.
"""
from pathlib import Path
import argparse,json,os,subprocess,sys,hashlib
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('--loss-fences',type=int,choices=range(2,9));a=p.parse_args()
out=a.output;receipt=out/'start.json'
if not receipt.exists():receipt=out/'result.json'
state=json.loads(receipt.read_text());assert state['watchdog']['armed'] and state['watchdog']['timeout_seconds']==60
compat=Path('/sys/firmware/devicetree/base/compatible').read_bytes()
if b'apple,j514s\0' in compat:chip='M3'
elif b'apple,j713\0' in compat:chip='M4'
else:raise RuntimeError('Unqualified target')
base=Path('/root/roadmap-fault-probe')
settings=dict(VK_DRIVER_FILES='/opt/asahi-m3m4-cts-20260927/asahi.json',VK_ICD_FILENAMES='/opt/asahi-m3m4-cts-20260927/asahi.json',LD_LIBRARY_PATH='/opt/asahi-m3m4-cts-20260927/lib',ASAHI_M3_EXPERIMENTAL='1',ASAHI_M4_EXPERIMENTAL='1',ASAHI_MESA_DEBUG='nocompress',HK_DEBUG_LOG='0',AGX_MESA_DEBUG='',VK_LAYER_PATH='/var/empty',VK_IMPLICIT_LAYER_PATH='/var/empty',M4_ORACLE_EXPECT_DEVICE_LOST='1',M4_ORACLE_DEVICE=chip)
if a.loss_fences:settings['M4_ORACLE_LOSS_FENCES']=str(a.loss_fences)
binary='vk-loss-fences-oracle' if a.loss_fences else 'vk-buffer-oracle'
env={k:v for k,v in os.environ.items() if not k.startswith(('VK_','MESA_','AGX_','HK_','ASAHI_','LD_','M4_ORACLE_'))};env.update(settings)
(out/'fault-probe-environment.json').write_text(json.dumps(settings,indent=2)+'\n')
(out/'fault-probe-inputs.json').write_text(json.dumps({f:hashlib.sha256((base/f).read_bytes()).hexdigest() for f in [binary,'probe.spv','input.bin','expected.bin']},indent=2)+'\n')
subprocess.run(['python3','/root/idle-display-session.py',str(out)],check=True,timeout=32)
cmd=[str(base/binary),str(base/'probe.spv'),str(base/'input.bin'),str(base/'expected.bin'),'1',str(out/'actual.bin')]
r=subprocess.run(cmd,env=env,timeout=15)
(out/'fault-probe-exit.json').write_text(json.dumps(dict(returncode=r.returncode))+'\n');os.sync()
# The guard must independently reject terminal unhealthy state even on rc=0.
raise SystemExit(r.returncode)
