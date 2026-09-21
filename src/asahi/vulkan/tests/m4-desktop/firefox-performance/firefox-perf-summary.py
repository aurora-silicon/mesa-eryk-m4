#!/usr/bin/env python3
"""Summarize retained gpu-experiment Firefox measurements without image review.
Usage: firefox-perf-summary.py _provenance/experiments/RUN [...]
Callback rates are browser diagnostics, not physical scanout FPS.
"""
import argparse
import collections
import json
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('runs', nargs='+', type=Path)
a = p.parse_args()
for run in a.runs:
    remote = run / 'remote'
    result = json.loads((remote / 'firefox/result.json').read_text())
    page = result.get('page', {})
    diagnostics = dict(page.get('diagnostics', {}))
    diagnostics.pop('videoFrames', None)
    elapsed = diagnostics.get('elapsed', 0)
    if elapsed:
        diagnostics['raf_callbacks_per_second'] = diagnostics['raf']['count'] / elapsed
        diagnostics['video_callbacks_per_second'] = diagnostics['video']['count'] / elapsed
    geometry = collections.defaultdict(lambda: [0, 0, 0])
    cache = []
    for line in (remote / 'kernel-live.log').read_text().splitlines():
        if 'render geometry summary' in line:
            values = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)}
            row = geometry[(values['asid'], values['width'], values['height'])]
            for i, key in enumerate(('commands', 'prepare_ns', 'fragment_ns')):
                row[i] += values[key]
        if 'idle owner cache' in line:
            cache.append(json.loads(line.split('=', 1)[1]))
    rows = [dict(asid=k[0], width=k[1], height=k[2], commands=v[0],
                 prepare_ms=v[1]/v[0]/1e6, fragment_ms=v[2]/v[0]/1e6)
            for k, v in sorted(geometry.items(), key=lambda kv: -kv[1][0])[:10]]
    summary = dict(run=run.name, completed=result.get('completed', False),
                   viewport=page.get('viewport'), playback=diagnostics, geometry=rows,
                   cache_columns=['bytes', 'hits', 'search_misses', 'puts', 'evictions', 'oversized', 'global_rejects'],
                   cache_first=cache[0] if cache else None, cache_last=cache[-1] if cache else None)
    print(json.dumps(summary, indent=2))
