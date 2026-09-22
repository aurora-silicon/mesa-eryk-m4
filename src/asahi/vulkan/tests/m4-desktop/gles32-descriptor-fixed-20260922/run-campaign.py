#!/usr/bin/env python3
"""Run the frozen campaign durably; save a report even when a guard stops it."""
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT = Path('/home/council/dev/dirtyroom_m4')
CAMPAIGN = ROOT/'build/gles32-descriptor-fixed-20260922'
state = dict(pid=os.getpid(), started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
             state='running', campaign=str(CAMPAIGN))
def save():
    tmp = CAMPAIGN/'controller.json.tmp'
    tmp.write_text(json.dumps(state, indent=2)+'\n')
    tmp.replace(CAMPAIGN/'controller.json')
save()
try:
    cmd = [sys.executable, str(ROOT/'tools/gl-cts-plan.py'), 'run', '--campaign', str(CAMPAIGN),
           '--continue-batch-deadlines', '--last', '9']
    state['command'] = cmd
    save()
    rc = subprocess.run(cmd, cwd=ROOT).returncode
    state.update(returncode=rc, state='selected-segments-complete' if rc == 0 else 'stopped-needs-review')
finally:
    with (CAMPAIGN/'report.log').open('w') as log:
        report = subprocess.run([sys.executable, str(ROOT/'tools/gl-cts-plan.py'), 'report',
                                 '--campaign', str(CAMPAIGN)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    state.update(report_returncode=report.returncode,
                 finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    save()
raise SystemExit(state.get('returncode', 1))
