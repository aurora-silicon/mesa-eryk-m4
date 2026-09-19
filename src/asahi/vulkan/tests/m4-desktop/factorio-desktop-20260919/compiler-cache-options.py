#!/usr/bin/env python3
"""Check M4 compiler-option cache separation; run under gpu-experiment.py."""
import json,os,subprocess,sys
from pathlib import Path
out=Path(sys.argv[1])
guard=json.loads((out/'start.json').read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds']==60
base=dict(HK_M4_PREFETCH_UBOS='false',HK_M4_PREFETCH_VARIANT='false',
          HK_M4_PLAIN_SAMPLERS='false',HK_M4_PREFETCH_DESCRIPTORS='false',
          HK_M4_VERSION_SAMPLES='true',HK_M4_NATIVE_PREAMBLE='false',AGX_NATIVE_PRESERVE_BRANCHES='false')
env=dict(os.environ,ASAHI_M4_EXPERIMENTAL='1',
         VK_DRIVER_FILES='/opt/asahi-m4-port/asahi_icd.json')
def key(flags):
    value=subprocess.check_output(['/root/vulkan-cache-key-probe'],
        env=dict(env,**flags),text=True,timeout=5).strip()
    assert len(value)==32 and int(value,16)!=0,value
    return value
keys={'base':key(base)}
for option,value in base.items():
    flags=dict(base)
    flags[option]='false' if value=='true' else 'true'
    keys[option]=key(flags)
assert len(set(keys.values()))==len(keys),keys
assert key(base)==keys['base']
(out/'compiler-cache-keys.json').write_text(json.dumps(keys,indent=2))
print('PASS: compiler options have distinct stable cache UUIDs',flush=True)
