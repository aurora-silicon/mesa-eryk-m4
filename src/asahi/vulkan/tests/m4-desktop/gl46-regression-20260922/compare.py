#!/usr/bin/env python3
"""Compare this campaign with retained latest outcomes; never hide unrun cases."""
from collections import Counter
import hashlib
import json
from pathlib import Path

campaign = Path(__file__).resolve().parent
baseline_file = campaign.parent / 'gl46-zink-20260922/final-report/latest-coverage.json'
current_file = campaign / 'report/latest-coverage.json'
before = json.loads(baseline_file.read_text())
after = json.loads(current_file.read_text())
original = {row['case']: row for row in before}
current = {row['case']: row for row in after}
assert len(original) == len(before) == len(current) == len(after) == 19714
assert original.keys() == current.keys()
changes = []
unrun = []
transitions = Counter()
for case, row in current.items():
    old = original[case]
    assert old['catalog_index'] == row['catalog_index']
    if row['status'] in ('Unrun', 'Interrupted'):
        unrun.append(dict(case=case, catalog_index=row['catalog_index'], status=row['status']))
        continue
    transitions[old['status'] + ' -> ' + row['status']] += 1
    if old['status'] != row['status']:
        changes.append(dict(case=case, catalog_index=row['catalog_index'],
                            before=old, after=row))
accepted = {'Pass', 'NotSupported', 'QualityWarning', 'CompatibilityWarning'}
report = dict(
    baseline=str(baseline_file), baseline_sha256=hashlib.sha256(baseline_file.read_bytes()).hexdigest(),
    current=str(current_file), current_sha256=hashlib.sha256(current_file.read_bytes()).hexdigest(),
    baseline_counts=dict(Counter(row['status'] for row in before)),
    current_counts=dict(Counter(row['status'] for row in after)),
    transitions=dict(transitions), changes=changes, unrun=unrun,
    lost_passes=[r for r in changes if r['before']['status'] == 'Pass' and r['after']['status'] != 'Pass'],
    new_failures=[r for r in changes if r['before']['status'] in accepted and r['after']['status'] not in accepted],
    limitation='Baseline spans builds and explicitly recorded alternate configurations. Current latest outcomes include separately retained alternate configurations. This is regression tracking, not certification.')
(campaign / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(dict(current_counts=report['current_counts'], transitions=report['transitions'],
                      changes=len(changes), lost_passes=len(report['lost_passes']),
                      new_failures=len(report['new_failures']), unrun=len(unrun)), indent=2))
