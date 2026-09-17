#!/usr/bin/env python3
"""Stage a frozen CTS list and measure one bounded, guarded deqp-runner run."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cases', type=Path, required=True)
p.add_argument('--runner', type=Path, required=True, help='ARM64 deqp-runner with retained local patch')
p.add_argument('--binary', default='/root/cts-1.0.2.6/deqp-vk-seed24-corrected')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--name', required=True)
p.add_argument('--renderer', choices=('m4', 'lavapipe'), default='m4', help='Guarded renderer for hardware/reference comparisons')
p.add_argument('--jobs', type=int, default=20)
p.add_argument('--cpu-max-khz', type=int, default=1860000, choices=[1860000, 4104000],
               help='CPU ceiling the guard applies during the run')
p.add_argument('--tests-per-group', type=int, default=50)
p.add_argument('--runner-seconds', type=int, default=90)
p.add_argument('--case-timeout', type=int, default=60, choices=range(1, 121),
               metavar='SECONDS', help='Explicit per-case wall-time bound; default 60s for contended and long CTS cases')
p.add_argument('--log-images', action='store_true', help='Retain reference/result PNGs for targeted rendering failures')
p.add_argument('--capture-success', action='store_true', help='Capture successful compiler inputs for offline diagnosis')
p.add_argument('--capture-before', action='store_true', help='Save compiler inputs before compilation to diagnose assertions')
p.add_argument('--log-sources', action='store_true', help='Retain CTS shader source in targeted runs')
p.add_argument('--headless', action='store_true', help='Private 1000 Hz compositor for one WSI worker')
p.add_argument('--env', action='append', metavar='KEY=VALUE', default=[],
               help='Recorded driver environment override passed to the remote runner')
a = p.parse_args()
if a.headless and a.jobs != 1:
    p.error('--headless requires --jobs 1 (one worker per compositor)')
if not re.fullmatch('[a-z0-9][a-z0-9-]*', a.name) or a.jobs < 1 or a.tests_per_group < 1 or not 1 <= a.runner_seconds <= 1700:
    p.error('invalid name or bounds')
raw = a.cases.read_bytes()
cases = raw.decode().splitlines()
if not cases or len(cases) != len(set(cases)):
    p.error('expected unique nonempty frozen case list')
# Reject mismatched catalog identities locally, before staging or arming a run.
sidecar = a.cases.with_suffix('.meta.json')
if sidecar.exists():
    metadata = json.loads(sidecar.read_text())
    if (metadata.get('schema') != 1 or
            metadata.get('cases_sha256') != hashlib.sha256(raw).hexdigest() or
            [item['case'] for item in metadata['cases']] != cases):
        p.error(f'case metadata does not match frozen case list: {sidecar}')
a.output.mkdir(parents=True, exist_ok=False)
remote = '/root/cts-bulk/' + a.name + '-' + hashlib.sha256(raw).hexdigest()[:12]
host = 'root@omarchy-m4'
prep = time.monotonic()
with (a.output/'stage.log').open('w') as log:
    subprocess.run(['ssh', host, 'mkdir -p ' + shlex.quote(remote)], check=True, stdout=log, stderr=subprocess.STDOUT)
    for source, dest in [(a.cases, 'cases.txt'), (a.runner, 'deqp-runner')]:
        subprocess.run(['scp', str(source), host+':'+remote+'/'+dest], check=True, stdout=log, stderr=subprocess.STDOUT)
    sidecar = a.cases.with_suffix('.meta.json')
    if sidecar.exists():
        subprocess.run(['scp', str(sidecar), host+':'+remote+'/cases.meta.json'], check=True, stdout=log, stderr=subprocess.STDOUT)
    else:
        # A previous labeled staging directory must not relabel a bare list.
        subprocess.run(['ssh', host, 'rm -f '+shlex.quote(remote+'/cases.meta.json')], check=True, stdout=log, stderr=subprocess.STDOUT)
    subprocess.run(['scp', str(ROOT/'tools/cts-sample.py'), str(ROOT/'tools/cts_deqp_backend.py'),
                    host+':'+remote+'/'], check=True, stdout=log, stderr=subprocess.STDOUT)
    if a.headless:
        subprocess.run(['scp', str(ROOT/'tools/cts-headless.py'), host+':'+remote+'/'],
                       check=True, stdout=log, stderr=subprocess.STDOUT)
preparation_seconds = time.monotonic()-prep
command = [sys.executable, str(ROOT/'tools/gpu-experiment.py'), '--name', a.name,
    '--hypothesis', 'Measure frozen CTS cases with grouped first-attempt execution',
    '--expect', '^CTS_BATCH_COMPLETE:', '--seconds', str(a.runner_seconds+20),
    '--cpu-max-khz', str(a.cpu_max_khz), '--',
    'python3', remote+'/cts-sample.py', 'batch', '--backend', 'deqp-runner',
    '--guard-output', '{output}', '--binary', a.binary, '--cases', remote+'/cases.txt',
    '--deqp-runner', remote+'/deqp-runner', '--count', str(len(cases)), '--jobs', str(a.jobs),
    '--tests-per-group', str(a.tests_per_group), '--runner-seconds', str(a.runner_seconds),
    '--case-timeout', str(a.case_timeout), '--renderer', a.renderer]
for option in ('log_images', 'log_sources', 'capture_success', 'capture_before'):
    if getattr(a, option):
        command.append('--' + option.replace('_', '-'))
for item in a.env:
    command += ['--env', item]
if a.headless:
    separator = command.index('--') + 1
    command[separator:separator] = ['python3', remote+'/cts-headless.py',
                                  '--guard-output', '{output}', '--']
begin = time.monotonic()
with (a.output/'experiment.log').open('w') as log:
    rc = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT).returncode
elapsed = time.monotonic()-begin
match = re.search(r'^Evidence: (.+)$', (a.output/'experiment.log').read_text(), re.M)
if not match:
    raise RuntimeError('No experiment evidence; inspect experiment.log')
evidence = Path(match[1])
report = json.loads((evidence/'report.json').read_text())
result_path = evidence/'remote/cts/results.json'
if result_path.exists():
    results = json.loads(result_path.read_text())
else:
    from cts_deqp_backend import collect
    identity = json.loads((evidence/'target-before.json').read_text())
    metadata = json.loads(a.cases.with_suffix('.meta.json').read_text()) if a.cases.with_suffix('.meta.json').exists() else None
    results = collect(evidence/'remote/cts', cases, metadata, 0, identity['boot_id'])
timing_path = evidence/'remote/cts/timing.json'
timing = json.loads(timing_path.read_text()) if timing_path.exists() else {}
counts = Counter(r['status'] for r in results)
summary = dict(selected=len(cases), recorded=len(results),
    completed=sum(r['status'] not in ('Unrun', 'Interrupted') for r in results),
    counts=counts,
    classifications=Counter(r['eligibility']['classification'] for r in results),
    elapsed_seconds=elapsed, preparation_seconds=preparation_seconds,
    runner_seconds=timing.get('elapsed_seconds'), jobs=a.jobs, tests_per_group=a.tests_per_group,
    case_timeout_seconds=a.case_timeout,
    cases_sha256=hashlib.sha256(raw).hexdigest(), evidence=str(evidence),
    guard_verdict=report['verdict'], command_returncode=rc,
    peak_temperature_c=report.get('observations', {}).get('peak_temperature_c'))
(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(summary,indent=2))
if rc or len(results) != len(cases):
    raise SystemExit(1)
