#!/usr/bin/env python3
"""Reconcile this frozen sample offline; keep baseline and CTS corrections explicit."""
from collections import Counter
import gzip
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def read(name):
    data = (ROOT / name).read_bytes()
    return json.loads(gzip.decompress(data) if name.endswith('.gz') else data)


baseline = read('baseline-results.json.gz')
audit = {r['case']: r for r in read('skip-audit-results.json.gz')}
cases = (ROOT / 'cases.txt').read_text().splitlines()
assert len(cases) == len(set(cases)) == 1000
assert set(cases) == set(audit) == {r['case'] for r in baseline}
records = {}
for row in baseline:
    name = row['case']
    records[name] = dict(case=name, original_status=row['status'],
                         audited_status=audit[name]['status'],
                         status=audit[name]['status'],
                         evidence='baseline-results.json.gz', corrected_cts=False,
                         attempts=[])

# Include failed intermediate attempts too; never select only passing rows.
for filename, corrected in [
    ('sparse-vertex-results.json', False),
    ('operand-results.json', False),
    ('scaled-blit-results.json', True),
    ('meta-results.json', False),
    ('scaled-types-results.json', True),
    ('ms-offset-results.json', False),
    ('buffer-gate-before-results.json', True),
    ('extended-gate-results.json', True),
    ('r8snorm-results.json', True),
    ('scaled-two-results.json', True),
    ('blit-values-results.json', True),
    ('scaled-blit-complete-results.json', True),
]:
    seen = set()
    for attempt in read(filename):
        name = attempt['case']
        assert name in records and name not in seen
        seen.add(name)
        record = records[name]
        record['attempts'].append(dict(status=attempt['status'], evidence=filename,
                                       binary=attempt['argv'][0], corrected_cts=corrected))
        record.update(status=attempt['status'], evidence=filename, corrected_cts=corrected)

rows = [dict(index=i, **records[name]) for i, name in enumerate(cases)]
counts = dict(Counter(r['status'] for r in rows))
summary = dict(
    seed=20260926, cases=len(rows), counts=counts,
    original_counts=dict(Counter(r['original_status'] for r in rows)),
    original_cts_passes=sum(r['status'] == 'Pass' and not r['corrected_cts'] for r in rows),
    corrected_cts_passes=sum(r['status'] == 'Pass' and r['corrected_cts'] for r in rows),
    original_failures_resolved=sum(r['original_status'] == 'Fail' and r['status'] == 'Pass' for r in rows),
    should_support_resolved=sum(r['audited_status'] == 'NotSupportedButShouldBeSupported' and r['status'] == 'Pass' for r in rows),
    baseline_runner_seconds=21.720,
    coverage='Baseline plus targeted retests, not a complete rerun on the final build. Corrected CTS passes are not unmodified CTS passes.',
)
assert counts == {'NotSupported': 546, 'Pass': 454}
assert summary['original_cts_passes'] == 439
assert summary['corrected_cts_passes'] == 15
assert summary['original_failures_resolved'] == 47
assert summary['should_support_resolved'] == 2
(ROOT / 'reconciled-results.json.gz').write_bytes(
    gzip.compress((json.dumps(rows, indent=2) + '\n').encode(), mtime=0))
(ROOT / 'reconciled-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
