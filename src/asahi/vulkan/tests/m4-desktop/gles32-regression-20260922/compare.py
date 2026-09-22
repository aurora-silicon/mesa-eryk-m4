#!/usr/bin/env python3
"""Compare indexed completed outcomes, retaining the source of each baseline."""
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path

p = Path(__file__).resolve().parent
root = p.parents[1]
cache = p / 'baseline.json.gz'
if not cache.exists():
    baseline = {}
    sources = [root / 'build/gles32-zink-20260922/full/results.json']
    fixes = root / 'build/gles32-fixes-20260922'
    sources += sorted((fixes / 'final-parallel').glob('*/run/results.json'))
    sources += sorted((fixes / 'timeouts-a').glob('*/run/results.json'))
    sources += [root / ('build/' + name + '/results.json') for name in
                ('gles32-retest-20260922', 'gles32-descriptor-fixed-20260922')]
    provenance = []
    for source in sources:
        raw = source.read_bytes()
        rows = json.loads(raw)
        provenance.append(dict(path=str(source), sha256=hashlib.sha256(raw).hexdigest()))
        for row in rows:
            if row['status'] in ('Unrun', 'Interrupted'):
                continue
            idx = row['eligibility']['catalog_index']
            previous = baseline.get(idx)
            assert previous is None or previous['case'] == row['case']
            baseline[idx] = dict(case=row['case'], status=row['status'],
                                 source=str(source), global_index=idx)
    cache.write_bytes(gzip.compress(json.dumps(list(baseline.values())).encode(), mtime=0))
    (p / 'baseline-sources.json').write_text(json.dumps(provenance, indent=2) + '\n')
baseline = {r['global_index']: r for r in json.loads(gzip.decompress(cache.read_bytes()))}
rows = json.loads((p / 'results.json').read_text())
transitions = Counter()
changes = []
new_coverage = Counter()
for row in rows:
    if row['status'] in ('Unrun', 'Interrupted'):
        continue
    old = baseline.get(row['global_index'])
    if old is None:
        new_coverage[row['status']] += 1
        continue
    assert old['case'] == row['case']
    transitions[old['status'] + ' -> ' + row['status']] += 1
    if old['status'] != row['status']:
        changes.append(dict(before=old, after=row))
report = dict(transitions=dict(transitions), new_coverage=dict(new_coverage),
              changes=changes, current_completed=len(rows),
              limitation='Historical baseline combines partial campaigns and explicitly selected hardware retests; it is not a complete single-build run. Unrun/Interrupted outcomes never replace completed outcomes.')
(p / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(dict(transitions=report['transitions'], changes=len(changes), new_coverage=report['new_coverage']), indent=2))
