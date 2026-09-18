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

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('log')
a = p.parse_args()
groups = defaultdict(list)
owner = None
times = []
for line in open(a.log):
    t = re.match(r'\[\s*([\d.]+)\]', line)
    if t:
        times.append(float(t[1]))
    render = re.search(r'owned render (\d+x\d+) ASID=(\d+)', line)
    compute = re.search(r'owned compute user context=(\d+)', line)
    if render:
        owner = (render[2], render[1])
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
                      groups=rows), indent=2))
