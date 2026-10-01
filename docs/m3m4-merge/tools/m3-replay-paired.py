#!/usr/bin/env python3
"""Compare two M3 prefixes in alternating, independently guarded replay pairs.

Requires an existing approved reference. Every check is compared exactly;
captures/settings and clocks are unchanged. Complete evidence stays in the
local benchmark tree. This tool neither changes desktop selection nor relaxes
image policy. Use benchmark-suite.py for cases with documented tolerances.
"""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

BENCH = Path('/home/council/dev/gpu-replay-bench')
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--case', required=True)
p.add_argument('--control', required=True)
p.add_argument('--candidate', required=True)
p.add_argument('--control-env', action='append', default=[])
p.add_argument('--candidate-env', action='append', default=[])
p.add_argument('--reference', required=True, type=Path)
p.add_argument('--pairs', type=int, default=9, choices=range(3, 11))
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()
a.reference = a.reference.resolve()
reference = json.loads(a.reference.read_text())
assert reference['case'] == a.case and reference['phase'] == 'check'
assert reference['success'] and reference['correctness'] == 'pass'
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=False)
env = dict(os.environ, GPU_BENCH_SESSION=a.output.name)
prefixes = dict(control=a.control, candidate=a.candidate)
options = dict(control=a.control_env, candidate=a.candidate_env)
records = []
checks = {}
boot = None
identities = {}


def run(label, phase):
    global boot
    command = ['python3', str(BENCH/'tools/replay-m3.py'), a.case,
               '--mesa-prefix', prefixes[label], '--mode', phase,
               '--timeout', '37']
    for value in options[label]:
        command += ['--diagnostic-env', value]
    index = len(records)
    (a.output/f'{index:02d}-command.json').write_text(
        json.dumps(command, indent=2)+'\n')
    with (a.output/f'{index:02d}-host.log').open('w') as log:
        result = subprocess.run(command, cwd=BENCH, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=150)
    text = (a.output/f'{index:02d}-host.log').read_text()
    paths = re.findall(r'^Evidence: (.+)$', text, re.M)
    assert len(paths) == 1, text
    evidence = Path(paths[0])
    assert result.returncode == 0, 'Replay failed; stop with evidence retained'
    guard = json.loads((evidence/'remote/result.json').read_text())
    assert guard['phase'] == 'complete' and guard['watchdog']['disarmed']
    assert guard['watchdog']['timeout_seconds'] == 60
    if boot is None:
        boot = guard['boot_id']
    assert guard['boot_id'] == boot, 'Boot changed during paired comparison'
    record = BENCH/'results'/a.case/'m3'/evidence.name/'benchmark.json'
    command = ['python3', str(BENCH/'tools/import-replay.py'), str(evidence),
               '--machine', 'm3']
    if phase != 'check':
        command += ['--check', str(checks[label])]
    subprocess.run(command, env=env, check=True, stdout=subprocess.DEVNULL)
    if phase == 'check':
        subprocess.run(['python3', str(BENCH/'tools/approve-check.py'),
                        str(record), '--reference', str(a.reference.resolve())],
                       check=True, stdout=subprocess.DEVNULL)
        checks[label] = record
    data = json.loads(record.read_text())
    assert data['success'] and data['correctness'] == 'pass'
    assert data['watchdog_verified'] and data['thermal_guard_passed']
    # The importer normalizes both GL and Vulkan manifests. Every timing also
    # has to match its own approved check's complete environment and identity.
    common = {k:data[k] for k in
              ('kernel', 'capture_sha256', 'replayer_sha256', 'mesa_sha256')}
    if label in identities:
        assert identities[label] == common, 'Binary/kernel/capture changed'
    else:
        identities[label] = common
    if len(identities) == 2:
        for k in ('kernel', 'capture_sha256', 'replayer_sha256'):
            assert identities['control'][k] == identities['candidate'][k], k
    row = dict(label=label, phase=phase, record=str(record), data=data)
    if phase != 'check':
        assert data['measured_frames'] == 30
        row['fps'] = 30/data['seconds']
    records.append(row)
    (a.output/'runs.json').write_text(json.dumps(records, indent=2)+'\n')
    print(json.dumps({k:v for k,v in row.items() if k != 'data'}), flush=True)
    return row


for phase in ('check', 'warmup'):
    for label in prefixes:
        run(label, phase)
pairs = []
for i in range(a.pairs):
    order = ('control', 'candidate') if i % 2 == 0 else ('candidate', 'control')
    pair = {label:run(label, 'time')['fps'] for label in order}
    pair['change_percent'] = (pair['candidate']/pair['control']-1)*100
    pairs.append(pair)
summary = dict(boot_id=boot, identities=identities, samples=pairs,
               control_median_fps=statistics.median(x['control'] for x in pairs),
               candidate_median_fps=statistics.median(x['candidate'] for x in pairs),
               median_paired_change_percent=statistics.median(
                   x['change_percent'] for x in pairs),
               positive_pairs=sum(x['change_percent'] > 0 for x in pairs),
               scope='30 completed replay frames; not live Java FPS')
(a.output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps(summary), flush=True)
