#!/usr/bin/env python3
"""Run consecutive prepared CTS segments through cts-bulk's existing guards.

Stops on the first fatal/incomplete segment for inspection. Never retries,
changes case membership, suppresses failures, or bypasses watchdog/thermals.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--batch', type=Path, required=True)
p.add_argument('--first-segment', type=int, default=1, help='One-based prepared segment number')
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--binary', required=True)
p.add_argument('--name', required=True)
p.add_argument('--jobs', type=int, default=20)
p.add_argument('--runner-seconds', type=int, default=900)
a = p.parse_args()
raw = (a.batch / 'cases.txt').read_bytes()
metadata = json.loads((a.batch / 'cases.meta.json').read_text())
assert hashlib.sha256(raw).hexdigest() == metadata['cases_sha256']
segments = sorted((a.batch / 'segments').glob('*/cases.txt'))
assert 1 <= a.first_segment <= len(segments)
combined, indices = [], []
for path in segments:
    data = path.read_bytes()
    sidecar = json.loads(path.with_suffix('.meta.json').read_text())
    assert hashlib.sha256(data).hexdigest() == sidecar['cases_sha256']
    names = data.decode().splitlines()
    assert names == [r['case'] for r in sidecar['cases']]
    combined.extend(names)
    indices.extend(r['catalog_index'] for r in sidecar['cases'])
assert combined == raw.decode().splitlines()
assert indices == [r['catalog_index'] for r in metadata['cases']]
selected = list(enumerate(segments, 1))[a.first_segment - 1:]
assert all(not (path.parent / 'run').exists() for _, path in selected), 'Refusing to overwrite a run'
began = time.monotonic()
for number, path in selected:
    output = path.parent / 'run'
    print(f'START segment {number}: {path.parent.name}', flush=True)
    argv = [sys.executable, str(ROOT / 'tools/cts-bulk.py'), '--cases', str(path),
            '--runner', str(a.runner), '--binary', a.binary, '--output', str(output),
            '--name', f'{a.name}-s{number:02d}', '--jobs', str(a.jobs),
            '--runner-seconds', str(a.runner_seconds), '--case-timeout', '60']
    rc = subprocess.run(argv, cwd=ROOT).returncode
    if rc:
        print(f'STOP segment {number}: inspect evidence before continuing; elapsed={time.monotonic()-began:.3f}s', flush=True)
        sys.exit(rc)
print(f'SERIES_COMPLETE: {len(selected)} segments; elapsed={time.monotonic()-began:.3f}s', flush=True)
