#!/usr/bin/env python3
"""Compare frozen indexed GL CTS outcomes from two full-catalog campaigns.

Usage: python3 tools/gl-cts-compare.py BASELINE CURRENT OUTPUT
Each argument is a campaign directory except OUTPUT, which is a JSON file.
Unrun/Interrupted in CURRENT are excluded until recorded; original outcomes
are retained in the comparison, including original Fail and Timeout.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline', type=Path)
p.add_argument('current', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
old = json.loads((a.baseline / 'catalog.json').read_text())
new = json.loads((a.current / 'catalog.json').read_text())
assert old['plan_sha256'] == new['plan_sha256'] and old['total'] == new['total']
assert old['sessions'] == new['sessions']
original = json.loads((a.baseline / 'results.json').read_text())
current = json.loads((a.current / 'results.json').read_text())
assert len(original) == old['total'] and len(current) <= old['total']
assert all(row['global_index'] == index for index, row in enumerate(original))
assert len({row['global_index'] for row in current}) == len(current)
transitions = Counter()
changes = []
for after in current:
    index = after['global_index']
    assert 0 <= index < old['total']
    before = original[index]
    assert before['global_index'] == index
    assert before['case'] == after['case']
    assert before['eligibility']['session'] == after['eligibility']['session']
    if after['status'] in ('Unrun', 'Interrupted'):
        continue
    transition = before['status'] + ' -> ' + after['status']
    transitions[transition] += 1
    if before['status'] != after['status']:
        changes.append({'global_index': index, 'case': after['case'],
                        'session': after['eligibility']['session'],
                        'before': before['status'], 'after': after['status'],
                        'original_seconds': before.get('seconds'),
                        'current_seconds': after.get('seconds')})
report = {'plan_sha256': old['plan_sha256'],
          'baseline_results_sha256': hashlib.sha256((a.baseline / 'results.json').read_bytes()).hexdigest(),
          'current_results_sha256': hashlib.sha256((a.current / 'results.json').read_bytes()).hexdigest(),
          'compared': sum(transitions.values()),
          'transitions': dict(transitions), 'changes': changes,
          'note': 'Compares exact configuration/case indices. Historical raw failures and timeouts are not rewritten.'}
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'compared': report['compared'], 'transitions': report['transitions'],
                  'changes': len(changes)}, indent=2))
