#!/usr/bin/env python3
"""Supervise a fresh pinned ES32 campaign, retaining each bounded attempt."""
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
state = dict(pid=os.getpid(), state='running', completed_segments=22,
             started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())

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
    for segment in catalog['segments'][22:]:
        index = segment['index']
        assert index == 22 or not (CAMPAIGN / 'segments' / f'{index:04d}' / 'run').exists()
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
        if index == 22:
            command += ['--recovery-review', str(CAMPAIGN / 'recovery-review-0022.json')]
        state.update(segment=index, command=command)
        save()
        print(f'START segment={index} start={segment["start"]} count={segment["count"]} jobs={segment["jobs"]}', flush=True)
        result = subprocess.run(command, cwd=ROOT)
        report()
        assert result.returncode == 0, f'Segment {index} stopped; review retained evidence'
        summary = json.loads((CAMPAIGN / 'summary.json').read_text())
        assert not any(summary['counts'].get(k, 0) for k in ('Crash', 'Timeout', 'GPUTimeout', 'HarnessError', 'InternalError', 'ResourceError'))
        state['completed_segments'] += 1
        state['completed_pairs'] = summary['completed']
        state['counts'] = summary['counts']
        save()
    state.update(state='complete-with-retained-incident', returncode=0)
except BaseException as error:
    state.update(state='stopped-needs-review', error=str(error), returncode=1)
    raise
finally:
    report()
    state['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save()
raise SystemExit(state['returncode'])
