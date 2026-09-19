#!/usr/bin/env python3
"""Run exact query-visibility modes inside gpu-experiment.py, stopping on failure.

Usage on target: query-visibility-probe.py GUARD_OUTPUT QUERY_BOUNDARY_BINARY
Modes include GPU/host resets, unavailable results, WAIT and NO-WAIT copies,
render/compute start/end timestamps and same/separate command-buffer submits.
Run WAIT modes only after the bounded NO-WAIT diagnostic has passed.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

out, binary = map(Path, sys.argv[1:])
start = json.loads((out / 'start.json').read_text())
assert start['watchdog']['armed'] and start['watchdog']['timeout_seconds'] == 60
results = {'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'modes': []}
for mode in [6, 0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12]:
    log = out / f'query-mode-{mode}.log'
    try:
        with log.open('w') as stream:
            result = subprocess.run([str(binary), str(mode)], stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=10)
        row = {'mode': mode, 'returncode': result.returncode,
               'pass': result.returncode == 0 and 'QUERY_COPY_PASS' in log.read_text()}
    except subprocess.TimeoutExpired:
        row = {'mode': mode, 'pass': False, 'timeout_seconds': 10}
    results['modes'].append(row)
    (out / 'query-visibility.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(row), flush=True)
    if not row['pass']:
        raise SystemExit(1)
print('PASS: 13 query visibility modes', flush=True)
