#!/usr/bin/env python3
"""Supervise repaired-kernel ES32 coverage with exact frozen identities."""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys

CAMPAIGN = Path(__file__).resolve().parent
ROOT = CAMPAIGN.parents[1]
catalog = json.loads((CAMPAIGN / 'catalog.json').read_text())
hashes = json.loads((CAMPAIGN / 'installed-hashes.json').read_text())
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--start', type=int, default=0,
                    help='First unfinished segment; validates the preceding frozen prefix')
args = parser.parse_args()
assert 0 <= args.start <= len(catalog['segments'])
if args.start:
    prior = json.loads((CAMPAIGN / 'summary.json').read_text())
    assert [run['segment'] for run in prior['runs']] == list(range(args.start))
    assert prior['completed'] == sum(s['count'] for s in catalog['segments'][:args.start])
    assert not any(prior['counts'].get(k, 0) for k in
                   ('Fail', 'Crash', 'Timeout', 'GPUTimeout', 'HarnessError',
                    'InternalError', 'ResourceError'))
    assert all(not (CAMPAIGN / 'segments' / f'{index:04d}' / 'run').exists()
               for index in range(args.start, len(catalog['segments'])))
else:
    assert not (CAMPAIGN / 'summary.json').exists() or json.loads(
        (CAMPAIGN / 'summary.json').read_text())['completed'] == 0
state = dict(pid=os.getpid(), state='running', completed_segments=args.start,
             started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
if args.start:
    state.update(completed_pairs=prior['completed'], counts=prior['counts'])

def save():
    tmp = CAMPAIGN / 'controller.json.tmp'
    tmp.write_text(json.dumps(state, indent=2) + '\n')
    tmp.replace(CAMPAIGN / 'controller.json')

def report():
    with (CAMPAIGN / 'report.log').open('w') as log:
        result = subprocess.run([sys.executable, str(ROOT / 'tools/gl-cts-plan.py'),
                                 'report', '--campaign', str(CAMPAIGN)],
                                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    state['report_returncode'] = result.returncode

save()
try:
    for segment in catalog['segments'][args.start:]:
        index = segment['index']
        assert not (CAMPAIGN / 'segments' / f'{index:04d}' / 'run').exists()
        cases = (CAMPAIGN / 'segments' / f'{index:04d}' / 'cases.txt').read_text().splitlines()
        slow_point_mode = ('KHR-GLES32.core.tessellation_shader.tessellation_shader_point_mode.points_verification'
                           in cases)
        observed = subprocess.check_output([
            'ssh', '-o', 'ConnectTimeout=10', 'root@omarchy-m4',
            'sha256sum ' + ' '.join(hashes)], text=True, timeout=20)
        assert {line.split()[1]: line.split()[0] for line in observed.splitlines()} == hashes
        watchdog = json.loads(subprocess.check_output([
            'ssh', '-o', 'ConnectTimeout=10', 'root@omarchy-m4',
            'python3 /root/ssh-watchdog.py status'], text=True, timeout=20))
        assert watchdog['armed'] and watchdog['timeout_seconds'] == 60 and watchdog['mode'] == 'idle'
        free = int(subprocess.check_output([
            'ssh', '-o', 'ConnectTimeout=10', 'root@omarchy-m4',
            "python3 -c 'import shutil; print(shutil.disk_usage(\"/root\").free)'"],
            text=True, timeout=20))
        assert free >= 2 * 1024**3, 'Less than 2 GiB target evidence space; stop for review'
        command = [sys.executable, str(ROOT / 'tools/gl-cts-plan.py'), 'run',
                   '--campaign', str(CAMPAIGN), '--first', str(index), '--last', str(index + 1),
                   '--continue-batch-deadlines', '--seconds', '900']
        if slow_point_mode:
            # Prior four-worker contention exceeded the unchanged 60s case
            # bound; a guarded serial run passed in 11.1s.
            command += ['--run-jobs', '1']
        state.update(segment=index, command=command)
        save()
        print(f'START segment={index} start={segment["start"]} count={segment["count"]} jobs={segment["jobs"]}', flush=True)
        result = subprocess.run(command, cwd=ROOT)
        report()
        assert result.returncode == 0, f'Segment {index} stopped; review retained evidence'
        summary = json.loads((CAMPAIGN / 'summary.json').read_text())
        assert not any(summary['counts'].get(k, 0) for k in ('Fail', 'Crash', 'Timeout', 'GPUTimeout', 'HarnessError', 'InternalError', 'ResourceError'))
        state['completed_segments'] += 1
        state['completed_pairs'] = summary['completed']
        state['counts'] = summary['counts']
        save()
    state.update(state='complete', returncode=0)
except BaseException as error:
    state.update(state='stopped-needs-review', error=str(error), returncode=1)
    raise
finally:
    report()
    state['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save()
raise SystemExit(state['returncode'])
