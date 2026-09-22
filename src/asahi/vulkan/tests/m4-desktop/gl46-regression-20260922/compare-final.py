#!/usr/bin/env python3
"""Compare with the latest retained baseline, identifying both CTS binaries."""
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path

p = Path(__file__).resolve().parent
root = p.parents[1]
baseline = root / 'mesa/m4-asahi/src/asahi/vulkan/tests/m4-desktop/gl46-cts/optional-features/reference/latest-catalog-with-cts-fix.json.gz'
before = json.loads(gzip.decompress(baseline.read_bytes()))
original = json.loads((p / 'report/latest-coverage.json').read_text())
corrected = json.loads((p / 'corrected-features/results.json').read_text())
summary = json.loads((p / 'corrected-features/summary.json').read_text())
assert summary['guard_verdict'] == 'pass' and summary['completed'] == 1914
assert summary['counts'] == {'Pass': 1914}, summary
index = {r['case']: r['catalog_index'] for r in original}
assert len(index) == len(original) == 19714
current = {r['case']: dict(r, cts_binary='upstream-f6a2970') for r in original}
for row in corrected:
    current[row['case']] = dict(row, catalog_index=index[row['case']],
                               run=str(p / 'corrected-features'),
                               cts_binary='corrected-de63a4f-0193725ec88f')
assert len(current) == 19714
changes = []
transitions = Counter()
for old in before:
    new = current[old['case']]
    assert new['catalog_index'] == old['catalog_index']
    transitions[old['status'] + ' -> ' + new['status']] += 1
    if old['status'] != new['status']:
        changes.append(dict(case=old['case'], catalog_index=old['catalog_index'],
                            before=old, after=new))
result = dict(baseline=str(baseline),
              baseline_sha256=hashlib.sha256(baseline.read_bytes()).hexdigest(),
              baseline_counts=dict(Counter(r['status'] for r in before)),
              original_cts_counts=dict(Counter(r['status'] for r in original)),
              corrected_selection_summary=summary,
              latest_counts=dict(Counter(r['status'] for r in current.values())),
              transitions=dict(transitions), changes=changes,
              classification='Eight query cases deliberately return NotSupported when ARB_sparse_texture2 is exposed (gl4cSparseTextureTests.cpp:453). Six original failures match previously fixed CTS setup defects; corrected binary results are separate.',
              limitation='Same installed driver throughout, two explicitly identified CTS binaries and alternate surface configurations. Development regression coverage, not certification.')
(p / 'final-comparison.json').write_text(json.dumps(result, indent=2) + '\n')
(p / 'latest-with-corrected-cts.json').write_text(json.dumps(list(current.values()), indent=2) + '\n')
print(json.dumps({k: result[k] for k in ('latest_counts', 'transitions')}, indent=2))
