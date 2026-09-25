#!/usr/bin/env python3
"""Run generated image64 oracles in one bounded M3 guard segment.

Usage under gpu/guard.py: image64-run.py CASE_DIR ORACLE OUTPUT [START [COUNT]]
Supports atomic64-image-, atomic64-sparse-image-, and formatless-cases.py.
Ordinary mismatches are retained for review; kernel faults retain the watchdog.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

root, oracle, out = map(Path, sys.argv[1:4])
start = int(sys.argv[4]) if len(sys.argv) > 4 else 0
count = int(sys.argv[5]) if len(sys.argv) > 5 else 1000
assert 'guard.py' in Path('/proc', str(os.getppid()), 'cmdline').read_text()
assert json.loads((out/'result.json').read_text())['phase'] == 'thermal-policy-ready'
cases = json.loads((root/'cases.json').read_text())
before = subprocess.check_output(['dmesg', '--color=never'], text=True)
stamp = float(re.findall(r'^\[\s*([0-9.]+)\]', before, re.M)[-1])
records = []
began = time.monotonic()
for index in range(start, min(len(cases), start + count)):
    if time.monotonic() - began > 25:
        break
    case = cases[index]
    name = case['name']
    sparse = 'shader' in case
    shader = root/((case['shader'] if sparse else name) + '.spv')
    initial = root/('input.bin' if sparse else name + '-input.bin')
    expected = root/(name + '.bin' if sparse else name + '-expected.bin')
    command = [str(oracle), str(shader), str(initial), str(expected),
               str(case.get('groups', 4)), str(out/(name+'.bin'))]
    env = dict(os.environ, **case['env'])
    record = dict(index=index, case=name, argv=command, environment=case['env'],
                  sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (oracle, shader, initial, expected)})
    started = time.monotonic()
    try:
        with (out/(name+'.log')).open('w') as log:
            proc = subprocess.run(command, env=env, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=8)
        record.update(returncode=proc.returncode,
                      status='Pass' if proc.returncode == 0 else 'Fail')
    except subprocess.TimeoutExpired:
        record.update(status='Timeout')
    record['seconds'] = time.monotonic() - started
    records.append(record)
    (out/'oracles.json').write_text(json.dumps(records, indent=2)+'\n')
    print(json.dumps(record), flush=True)
    after = subprocess.check_output(['dmesg', '--color=never'], text=True)
    delta = []
    for line in after.splitlines():
        match = re.match(r'^\[\s*([0-9.]+)\]', line)
        if match and float(match[1]) > stamp:
            delta.append(line)
    delta = '\n'.join(delta)
    (out/'kernel-delta.log').write_text(delta+'\n')
    if re.search(r'BUG:|Oops:|Kernel panic|SError|firmware crashed|execution failed|'
                 r'asahi .*M3.*(?:timeout|failed)', delta, re.I):
        raise RuntimeError('Kernel fault; retain watchdog recovery')
    if record['status'] != 'Pass':
        break
(out/'summary.json').write_text(json.dumps(dict(
    next_index=start+len(records), total=len(cases),
    passed=sum(r['status']=='Pass' for r in records), count=len(records)), indent=2)+'\n')
