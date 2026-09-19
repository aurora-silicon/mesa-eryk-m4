#!/usr/bin/env python3
"""Bounded native query-copy flag matrix; run under gpu-experiment.py."""
import argparse,json,os,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('--probe',default='/root/vulkan-query-variant-probe');a=p.parse_args()
out=a.output;g=json.loads((out/'start.json').read_text())
assert g['watchdog']['armed'] and g['watchdog']['timeout_seconds']==60
cases=[(16,f) for f in range(8)]+[(17,f) for f in (0,1,4,5)]
for mode,flags in cases:
    log=out/f'query-{mode}-{flags}.log'
    with log.open('w') as f:
        subprocess.run([a.probe,str(mode),str(flags)],
                       stdout=f,stderr=subprocess.STDOUT,check=True,timeout=8)
    assert 'QUERY_VARIANT_PASS' in log.read_text()
    print(f'checked mode={mode} flags={flags}',flush=True)
print('PASS: all 12 query-copy flag variants and untouched bytes',flush=True)
