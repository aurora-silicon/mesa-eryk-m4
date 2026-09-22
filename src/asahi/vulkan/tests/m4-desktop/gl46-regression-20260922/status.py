#!/usr/bin/env python3
"""Read this campaign's saved totals and a bounded tail of its active runner."""
import json
from pathlib import Path
import subprocess

campaign = Path(__file__).resolve().parent
state = json.loads((campaign / 'controller.json').read_text())
summary = json.loads((campaign / 'report/summary.json').read_text())
result = dict(state=state['state'], segment=state['segment'],
              completed_segments=state['completed_segments'], counts=summary['counts'],
              peak_temperature_c=max(r['peak_temperature_c'] for r in summary['runs']),
              error=state.get('error'))
if state['state'] == 'running':
    script = '''from pathlib import Path
from collections import Counter
import json, re
paths = sorted(Path('/root/asahi-evidence').glob('*-gl46-regression-%04d'))
if paths:
    p = paths[-1]
    log = p/'cts/runner.log'
    if log.exists():
        with log.open('rb') as stream:
            stream.seek(max(0, log.stat().st_size-10000))
            lines = stream.read().decode(errors='replace').splitlines()
        outcomes = Counter()
        for qpa in (p/'cts/deqp-runner').glob('c*.qpa'):
            with qpa.open(errors='replace') as stream:
                for line in stream:
                    match = re.search('<Result StatusCode="([^"]+)"', line)
                    if match:
                        outcomes[match[1]] += 1
        print(json.dumps(dict(evidence=p.name, progress=[s for s in lines if 'Duration:' in s][-1:], qpa_outcomes=outcomes)))
''' % state['segment']
    probe = subprocess.run(['ssh', '-o', 'ConnectTimeout=10', 'root@omarchy-m4', 'python3', '-'],
                           input=script, capture_output=True, text=True, timeout=20)
    result['live'] = json.loads(probe.stdout) if probe.returncode == 0 and probe.stdout.strip() else probe.stderr
print(json.dumps(result, indent=2))
