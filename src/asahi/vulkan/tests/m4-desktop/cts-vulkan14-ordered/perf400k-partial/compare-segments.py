#!/usr/bin/env python3
"""Compare run2 segment outcomes with earlier ordered runs (offline)."""
import json, glob, sys, collections
def load(path):
    try: r=json.load(open(path))
    except Exception: return {}
    items=r if isinstance(r,list) else r.get('results',r); items=items if isinstance(items,list) else list(items.values())
    return {i['case']: i['status'] for i in items}
base={}
for f in glob.glob('build/cts-vulkan14-ordered/0*/segments/*/run/results.json'):
    base.update(load(f))
new_fail=[]; tot=collections.Counter(); segs=0
for f in sorted(glob.glob('build/cts-vulkan14-ordered-perf/run2/0000000-0400000/segments/*/run/results.json')):
    cur=load(f); segs+=1
    for c,s in cur.items():
        tot[s]+=1
        if s in ('Fail','Crash','Timeout') and base.get(c) in ('Pass','NotSupported'):
            new_fail.append((f.split('/')[-3], s, c, base.get(c)))
        elif s in ('Fail','Crash','Timeout') and c not in base:
            new_fail.append((f.split('/')[-3], s, c, 'no-baseline'))
print('segments', segs, dict(tot))
for x in new_fail: print(*x)
