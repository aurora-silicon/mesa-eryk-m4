#!/usr/bin/env python3
"""Retest recorded GL failures with their original configuration and indices.

Uses cts-bulk's mandatory watchdog/thermal/evidence guards. Original campaign
results are never modified. Stops on crashes/timeouts/guard faults for review.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--campaign', required=True, type=Path)
p.add_argument('--failures', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--status', action='append', required=True)
p.add_argument('--session', type=int, action='append')
p.add_argument('--seconds', type=int, default=600)
p.add_argument('--env', action='append', default=[])
p.add_argument('--renderer', choices=('m4', 'lavapipe'), default='m4')
a = p.parse_args()
catalog = json.loads((a.campaign/'catalog.json').read_text())
env = json.loads((a.campaign/'environment.json').read_text())
if a.renderer == 'lavapipe':
    env.update(VK_DRIVER_FILES='/usr/share/vulkan/icd.d/lvp_icd.json',
               VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/lvp_icd.json',
               LIBGL_ALWAYS_SOFTWARE='1')
env.update(dict(v.split('=', 1) for v in a.env))
rows = [r for r in json.loads(a.failures.read_text()) if r['status'] in a.status
        and (a.session is None or r['session'] in a.session)]
assert rows
a.output.mkdir(parents=True, exist_ok=False)
(a.output/'selection.json').write_text(json.dumps(rows, indent=2)+'\n')
for session in sorted({r['session'] for r in rows}):
    selected = [r for r in rows if r['session'] == session]
    for first in range(0, len(selected), 50):
        group = selected[first:first+50]
        folder = a.output/f'{session:03d}-{first:04d}'
        folder.mkdir()
        raw = ('\n'.join(r['case'] for r in group)+'\n').encode()
        (folder/'cases.txt').write_bytes(raw)
        (folder/'cases.meta.json').write_text(json.dumps(dict(schema=1,
            cases_sha256=hashlib.sha256(raw).hexdigest(),
            provenance=dict(plan_sha256=catalog['plan_sha256'], retest_source=str(a.failures)),
            cases=[r['eligibility'] for r in group]), indent=2)+'\n')
        cmd = [sys.executable, str(ROOT/'tools/cts-bulk.py'), '--cases', str(folder/'cases.txt'),
            '--output', str(folder/'run'), '--name', 'gles-fix-'+a.output.name+f'-{session}-{first}',
            '--binary', catalog['binary'], '--runner', str(ROOT/'reference/deqp-runner/target/aarch64-unknown-linux-gnu/release/deqp-runner'),
            '--jobs', '1', '--runner-seconds', str(a.seconds), '--case-timeout', '60',
            '--cpu-max-khz', '4104000', '--headless', '--renderer', a.renderer,
            '--log-images', '--log-sources']
        for opt in catalog['sessions'][session]['options'] + ['--deqp-egl-display-type=x11', '--deqp-log-flush=enable']:
            cmd += ['--deqp-arg='+opt]
        for key in ('MESA_GL_VERSION_OVERRIDE', 'MESA_GLES_VERSION_OVERRIDE', 'MESA_GLSL_VERSION_OVERRIDE'):
            cmd += ['--unset-env', key]
        for key, value in env.items():
            if key.startswith(('GALLIUM','MESA','VK_','__EGL','LD_LIBRARY','LIBGL','ASAHI','AGX_','HK_')):
                cmd += ['--env', key+'='+value]
        (folder/'command.json').write_text(json.dumps(cmd, indent=2)+'\n')
        result = subprocess.run(cmd, cwd=ROOT)
        if result.returncode:
            raise SystemExit(result.returncode)
