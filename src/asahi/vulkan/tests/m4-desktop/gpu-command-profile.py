#!/usr/bin/env python3
"""Summarize HK_COMMAND_PROFILE logs; command counts are not GPU utilization/FPS."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('log', type=Path)
p.add_argument('--tail-seconds', type=float, default=20)
a = p.parse_args()
rows = []
for line in a.log.read_text(errors='replace').splitlines():
    if not line.startswith('HK_COMMAND_PROFILE '):
        continue
    fields = dict(re.findall(r'(\w+)=(\w+)', line))
    rows.append({k: v if k == 'engine' else int(v) for k, v in fields.items()})
if not rows:
    raise SystemExit('No HK_COMMAND_PROFILE records')
rows = [r for r in rows if r['ns'] >= rows[-1]['ns'] - a.tail_seconds * 1e9]
keys = ('timestamp_only', 'fills', 'timestamp_copies', 'query_copies',
        'precomp_calls', 'retirement_copies')
origins = Counter(tuple([r['engine']] + [r.get(k, 0) for k in keys]) for r in rows)
print(json.dumps(dict(log=str(a.log), retained_seconds=(rows[-1]['ns']-rows[0]['ns'])/1e9,
    commands_by_engine=dict(Counter(r['engine'] for r in rows)),
    operation_totals={k: sum(r.get(k, 0) for r in rows) for k in keys},
    precompiled_ids=dict(Counter(str(r['precomp_id']) for r in rows if r.get('precomp_calls'))),
    origin_fields=['engine', *keys], origin_counts=origins.most_common(),
    note='CPU submission records under opt-in logging; not execution timing or FPS'), indent=2))
