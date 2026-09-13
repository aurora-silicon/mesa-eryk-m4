#!/usr/bin/env python3
"""Run consecutive CTS batches through gpu-experiment.py; preserve every outcome.

Each batch has its own 40-second watchdog/thermal lease. Never retry tests or
recover hardware automatically. Stop on guard/transport errors and timeouts.
Use --continue-userspace-crashes only for a campaign containing known process
crashes; they remain Crash results and may continue only after full guard success.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--name', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cases', default='/root/cts-1.0.2.6/cases.txt')
p.add_argument('--binary', default='/root/cts-1.0.2.6/deqp-vk')
p.add_argument('--start', type=int, default=0)
p.add_argument('--count', type=int, required=True)
p.add_argument('--jobs', type=int, default=1)
p.add_argument('--continue-userspace-crashes', action='store_true')
a = p.parse_args()
if not re.fullmatch('[a-z0-9][a-z0-9-]*', a.name) or a.start < 0 or a.count < 1 or a.jobs < 1:
    p.error('invalid name, start or count')
a.output.mkdir(parents=True, exist_ok=False)
results, batches = [], []
identity = None
started_unix = time.time()
started_monotonic = time.monotonic()
batch_execution_seconds = 0.0
end = a.start + a.count
while a.start + len(results) < end:
    start = a.start + len(results)
    command = [sys.executable, str(ROOT/'tools/gpu-experiment.py'),
               '--name', f'{a.name}-{start:03d}', '--hypothesis',
               'Fixed CTS campaign; retain every result without exclusions or retries',
               '--expect', '^CTS_BATCH_COMPLETE:', '--seconds', '40', '--',
               'python3', '/root/cts-sample.py', 'batch', '--guard-output', '{output}',
               '--binary', a.binary, '--cases', a.cases,
               '--start', str(start), '--count', str(end-start), '--jobs', str(a.jobs)]
    log = a.output/f'batch-{start:03d}.log'
    with log.open('w') as stream:
        run = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    match = re.search(r'^Evidence: (.+)$', log.read_text(), re.M)
    if not match:
        raise RuntimeError(f'No fetched evidence; stopped, see {log}')
    evidence = Path(match[1])
    report = json.loads((evidence/'report.json').read_text())
    guard = json.loads((evidence/'remote/result.json').read_text())
    before = json.loads((evidence/'target-before.json').read_text())
    thermal, watchdog = guard['thermal'], guard['watchdog']
    if (not report['evidence_fetched'] or guard['returncode'] != 0
        or guard['boot_id'] != before['boot_id']
        or watchdog['timeout_seconds'] != 60 or not watchdog['armed']
        or thermal['failure'] is not None or thermal['samples'] < 1):
        raise RuntimeError(f'Guard/experiment failure; stopped at {evidence}')
    current_identity = [before['boot_id'], before['files'],
                        json.loads((evidence/'remote/cts/binary.json').read_text())]
    if identity is not None and identity != current_identity:
        raise RuntimeError(f'Boot/binary/guard identity changed at {evidence}')
    identity = current_identity
    batch = json.loads((evidence/'remote/cts/results.json').read_text())
    if not batch or [x['index'] for x in batch] != list(range(start, start+len(batch))):
        raise RuntimeError(f'Missing or discontinuous results at {evidence}')
    device_lost = any('VK_ERROR_DEVICE_LOST' in
        (evidence/'remote/cts'/f'{x["index"]:03d}'/'stdout.log').read_text()
        for x in batch)
    timing = json.loads((evidence/'remote/cts/timing.json').read_text())
    batch_execution_seconds += timing['elapsed_seconds']
    results.extend(batch)
    batches.append(str(evidence))
    (a.output/'results.json').write_text(json.dumps(results, indent=2)+'\n')
    (a.output/'summary.json').write_text(json.dumps(dict(
        requested=a.count, completed=len(results), jobs=a.jobs,
        started_unix=started_unix, finished_unix=time.time(),
        elapsed_seconds=time.monotonic()-started_monotonic,
        batch_execution_seconds=batch_execution_seconds, counts=Counter(x['status'] for x in results),
        batches=batches, identity=identity, last_guard_verdict=report['verdict']), indent=2)+'\n')
    print(f'{len(results)}/{a.count}: {dict(Counter(x["status"] for x in results))}; {evidence.name}', flush=True)
    # Persist completed CTS outcomes even when the guard reports a GPU fault.
    # Recovery still requires an explicit inspection and a new segment.
    if report['verdict'] not in ('pass', 'shader_error', 'api_error', 'readback_mismatch'):
        raise RuntimeError(f'Guard/experiment failure; outcomes retained at {evidence}')
    if device_lost or any(x['status'] in ('Timeout', 'HarnessError') or
           (x['status'] == 'Crash' and not a.continue_userspace_crashes) for x in batch):
        raise RuntimeError(f'Case stopped campaign; evidence retained at {evidence}')
