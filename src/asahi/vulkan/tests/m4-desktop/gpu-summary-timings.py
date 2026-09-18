#!/usr/bin/env python3
"""Summarize SubmitSummary retirement intervals across all GPU clients.

Use --tail-seconds to exclude initialization. GPU spans include TA-to-fragment
latency; active spans include firmware pickup and host observation. Neither is
a hardware utilization counter. Only fully contained intervals are selected.
"""
import argparse
import json
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('log', type=Path)
p.add_argument('--tail-seconds', type=float)
a = p.parse_args()
lines = a.log.read_text().splitlines()
rows = []
for line in lines:
    if 'userspace GPU job' not in line or 'summary_span_ns=' not in line:
        continue
    rows.append({k:int(v) for k,v in re.findall(r'\b([a-z_]+)=(-?\d+)', line)})
if not rows:
    p.error('No SubmitSummary intervals')
end = rows[-1]['monotonic_ns']
if a.tail_seconds is not None:
    start = end - a.tail_seconds * 1e9
    rows = [r for r in rows if r['monotonic_ns'] - r['summary_span_ns'] >= start]
if not rows:
    p.error('No complete intervals in requested window')
sums = {k:sum(r[k] for r in rows) for k in rows[0] if k != 'monotonic_ns'}
result = dict(intervals=len(rows), span_seconds=sums['summary_span_ns']/1e9,
              scope='All clients; timestamp spans, not hardware utilization', engines={})
for engine in ['render','compute']:
    n=sums[engine+'_commands']
    d=dict(commands=n)
    for name in ['prepare','active','gpu']:
        ns=sums[f'{engine}_{name}_ns']
        d[name+'_seconds']=ns/1e9
        d[name+'_mean_ms']=ns/1e6/n if n else None
    result['engines'][engine]=d
if 'render_private_clear_bytes' in sums:
    n = sums['render_commands']
    result['render_clears'] = dict(
        private_bytes=sums['render_private_clear_bytes'],
        private_mean_bytes=sums['render_private_clear_bytes']/n if n else None,
        private_seconds=sums['render_private_clear_ns']/1e9,
        total_seconds=sums['render_clear_ns']/1e9)
if 'render_tiling_ns' in sums:
    result['render_stages_seconds'] = {name:sums['render_'+name+'_ns']/1e9
                                       for name in ['tiling','stage_gap','fragment']}
selected = {r['monotonic_ns'] for r in rows}
geometry = {}
waits = []
for line in lines:
    if 'render geometry summary' not in line and 'completion wait totals' not in line:
        continue
    r = {k:int(v) for k,v in re.findall(r'\b([a-z_]+)=(-?\d+)', line)}
    if 'completion wait totals' in line:
        waits.append(r)
        continue
    if r['monotonic_ns'] not in selected:
        continue
    key = (r['asid'], r['width'], r['height'])
    g = geometry.setdefault(key, dict(asid=key[0], width=key[1], height=key[2],
        commands=0, prepare_ns=0, active_ns=0, fragment_ns=0, max_fragment_ns=0))
    for name in ['commands', 'prepare_ns', 'active_ns', 'fragment_ns']:
        g[name] += r[name]
    g['max_fragment_ns'] = max(g['max_fragment_ns'], r['max_fragment_ns'])
if geometry:
    result['render_geometry'] = sorted(geometry.values(), key=lambda g:g['fragment_ns'], reverse=True)
if waits:
    included = [w for w in waits if w['monotonic_ns'] in selected]
    prior = [w for w in waits if w['monotonic_ns'] < min(selected)]
    # Cumulative counters require a preceding sample. Without one, omit the
    # first interval from the deltas rather than including the whole boot.
    if included and (prior or len(included) > 1):
        first = prior[-1] if prior else included[0]
        last = included[-1]
        result['completion_wait_deltas'] = {
            k:last[k]-first[k] for k in ['messages','waits','late','timeouts']}
        result['completion_wait_deltas']['span_seconds'] = (last['monotonic_ns']-first['monotonic_ns'])/1e9
        result['completion_wait_deltas']['geometry_overflow'] = sum(w['geometry_overflow'] for w in included)
print(json.dumps(result,indent=2))
