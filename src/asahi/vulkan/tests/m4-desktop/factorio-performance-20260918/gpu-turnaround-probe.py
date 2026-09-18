#!/usr/bin/env python3
"""Measure one bounded stream of tiny, checked GPU commands under gpu-experiment."""
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

out = Path(sys.argv[1])
guard = json.loads((out / 'start.json').read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds'] == 60
env = dict(os.environ, VK_DRIVER_FILES='/opt/asahi-m4-port/asahi_icd.json',
           ASAHI_M4_EXPERIMENTAL='1', ASAHI_MESA_DEBUG='nocompress')
(out / 'go').touch()
(out / 'counter-before.txt').write_text(subprocess.check_output(
    ['/root/counter-clock'], text=True, timeout=3))
subprocess.run(['/root/vk-priority-probe', '/root/load.spv', '1', '64', '64', '32',
                str(out / 'ready'), str(out / 'go'), str(out / 'commands.jsonl')],
               env=env, check=True, timeout=25)
(out / 'counter-after.txt').write_text(subprocess.check_output(
    ['/root/counter-clock'], text=True, timeout=3))
rows = [json.loads(s) for s in (out / 'commands.jsonl').read_text().splitlines()]
times = sorted(r['latency_ms'] / 64 for r in rows[1:])
summary = dict(commands=64 * len(rows), milliseconds_per_command=dict(
    min=times[0], median=statistics.median(times), p95=times[int(len(times)*.95)], max=times[-1]))
(out / 'summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary), flush=True)
print('PASS: tiny-command turnaround and every readback', flush=True)
