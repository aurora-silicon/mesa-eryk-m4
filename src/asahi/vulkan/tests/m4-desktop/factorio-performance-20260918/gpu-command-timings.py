#!/usr/bin/env python3
"""Summarize serialized M4 retirement logs; GPU spans are not utilization.

Usage: gpu-command-timings.py EXPERIMENT/remote/kernel-live.log
Reports each ASID and render geometry separately. Preparation/active durations
are host timings; GPU render spans include the TA-to-fragment interval.
"""
import argparse
from collections import defaultdict
import json
import re
import statistics
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('log')
p.add_argument('--tail-seconds', type=float, help='Restrict to the last N seconds in this log')
p.add_argument('--frame-geometry', help='A known once-per-frame pass, e.g. 628x388; reports inferred frame cadence')
a = p.parse_args()
lines = Path(a.log).read_text().splitlines()
if a.tail_seconds is not None:
    stamps = [float(m[1]) for line in lines if (m := re.match(r'\[\s*([\d.]+)\]', line))]
    cutoff = max(stamps) - a.tail_seconds
    lines = [line for line in lines if (m := re.match(r'\[\s*([\d.]+)\]', line)) and float(m[1]) >= cutoff]
groups = defaultdict(list)
frames = defaultdict(list)
owner = None
times = []
for line in lines:
    t = re.match(r'\[\s*([\d.]+)\]', line)
    if t:
        times.append(float(t[1]))
    render = re.search(r'owned render (\d+x\d+) ASID=(\d+)', line)
    compute = re.search(r'owned compute user context=(\d+)', line)
    if render:
        owner = (render[2], render[1])
        if a.frame_geometry == render[1] and t:
            frames[render[2]].append(float(t[1]))
    elif compute:
        owner = (compute[1], 'compute')
    if 'userspace GPU job' in line and 'retired' in line:
        fields = {k: int(v) / 1e6 for k, v in re.findall(r'last_(\w+)_ns=(\d+)', line)}
        if owner and fields:
            groups[owner].append(fields)
        owner = None
rows = []
for (asid, kind), values in sorted(groups.items()):
    row = dict(asid=int(asid), kind=kind, commands=len(values))
    for metric in ['prepare', 'active', 'cleanup', 'gpu']:
        data = sorted(v[metric] for v in values)
        row[metric+'_ms'] = dict(total=sum(data), median=statistics.median(data),
                                p95=data[min(len(data)-1, int(len(data)*.95))])
    rows.append(row)
print(json.dumps(dict(log=a.log, span_seconds=times[-1]-times[0] if times else 0,
                      groups=rows, inferred_frames=[dict(asid=int(asid), geometry=a.frame_geometry, count=len(v), span_seconds=v[-1]-v[0], fps=(len(v)-1)/(v[-1]-v[0])) for asid,v in frames.items() if len(v)>1 and v[-1]>v[0]]), indent=2))
