#!/usr/bin/env python3
"""Continue segments 39-41 after review-0038.json; retain all original failures."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

CAMPAIGN = Path(__file__).resolve().parent
ROOT = CAMPAIGN.parents[1]
plan = json.loads((CAMPAIGN / 'plan.json').read_text())
hashes = json.loads((CAMPAIGN / 'installed-hashes.json').read_text())
state = dict(pid=os.getpid(), state='running', completed_segments=39,
             started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())


def save():
    tmp = CAMPAIGN / 'controller.json.tmp'
    tmp.write_text(json.dumps(state, indent=2) + '\n')
    tmp.replace(CAMPAIGN / 'controller.json')


def report():
    runs = [CAMPAIGN / 'segments' / f'{s["index"]:04d}' / 'run'
            for s in plan['segments']]
    runs = [p for p in runs if (p / 'summary.json').is_file()]
    if runs:
        with (CAMPAIGN / 'report.log').open('w') as log:
            result = subprocess.run([
                sys.executable, str(ROOT / 'tools/gl-cts-report.py'),
                '--catalog', str(CAMPAIGN / 'catalog.json'), '--latest-summary',
                '--output', str(CAMPAIGN / 'report'), *map(str, runs)],
                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        state['report_returncode'] = result.returncode


save()
try:
    for segment in plan['segments'][39:]:
        index = segment['index']
        folder = CAMPAIGN / 'segments' / f'{index:04d}'
        assert not (folder / 'run').exists(), 'Refusing to overwrite an attempt'
        assert hashlib.sha256((folder / 'cases.txt').read_bytes()).hexdigest() == segment['sha256']
        observed = subprocess.check_output([
            'ssh', '-o', 'ConnectTimeout=10', 'root@omarchy-m4',
            'sha256sum ' + ' '.join(hashes)], text=True, timeout=20)
        assert {line.split()[1]: line.split()[0] for line in observed.splitlines()} == hashes
        watchdog = json.loads(subprocess.check_output([
            'ssh', '-o', 'ConnectTimeout=10', 'root@omarchy-m4',
            'python3 /root/ssh-watchdog.py status'], text=True, timeout=20))
        assert watchdog['armed'] and watchdog['timeout_seconds'] == 60 and watchdog['mode'] == 'idle'
        command = [sys.executable, str(ROOT / 'tools/gl33-cts-run.py'),
                   '--api', 'gl46', '--cases', str(folder / 'cases.txt'),
                   '--output', str(folder / 'run'), '--name', f'gl46-regression-{index:04d}',
                   '--seconds', '900', '--jobs', str(segment['jobs']),
                   '--config', segment['config'], '--environment', str(CAMPAIGN / 'environment.json')]
        (folder / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
        state.update(segment=index, command=command)
        save()
        print(f'START segment={index} count={segment["count"]} jobs={segment["jobs"]} config={segment["config"]}', flush=True)
        result = subprocess.run(command, cwd=ROOT)
        summary = json.loads((folder / 'run/summary.json').read_text())
        assert result.returncode == 0, f'Segment {index} returned {result.returncode}; retained for review'
        assert summary['guard_verdict'] == 'pass' and summary['completed'] == segment['count'], summary
        assert set(summary['counts']) <= {'Pass', 'NotSupported', 'QualityWarning', 'CompatibilityWarning'}, summary
        state['completed_segments'] += 1
        report()
        save()
    state.update(state='complete-with-failures', returncode=0, retained_failure_count=6)
except BaseException as error:
    state.update(state='stopped-needs-review', error=str(error), returncode=1)
    raise
finally:
    report()
    state['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save()
