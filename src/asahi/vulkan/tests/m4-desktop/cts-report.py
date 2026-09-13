#!/usr/bin/env python3
"""Aggregate a frozen CTS sample across guarded campaign/recovery segments.

Offline only. Reject duplicate attempts and mismatched case indices. Unrun
cases stay Unrun; do not infer Pass from a guard verdict. Inspect each recovery
before continuing the campaign; this tool never executes or retries tests.
"""
import argparse
from collections import Counter
import hashlib
import csv
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cases', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('segments', type=Path, nargs='+')
a = p.parse_args()
raw = a.cases.read_bytes()
cases = raw.decode().splitlines()
assert len(cases) == len(set(cases))
records, batches, identities, timings = {}, [], [], []
for segment in a.segments:
    summary = json.loads((segment/'summary.json').read_text())
    identities.append(summary['identity'])
    if 'elapsed_seconds' in summary:
        timings.append({k: summary[k] for k in ('jobs', 'started_unix',
            'finished_unix', 'elapsed_seconds', 'batch_execution_seconds')})
    for name in summary['batches']:
        if name not in batches:
            batches.append(name)
    for record in json.loads((segment/'results.json').read_text()):
        index = record['index']
        if index in records:
            raise ValueError(f'Duplicate attempt for index {index}; keep retests separate')
        assert 0 <= index < len(cases) and record['case'] == cases[index]
        records[index] = record

# Reboots may change boot identity, but not the installed software or guards.
for identity in identities[1:]:
    if identity[1:] != identities[0][1:]:
        raise ValueError('Campaign software/binary identity changed')

results = [records.get(i, dict(index=i, case=case, status='Unrun'))
           for i, case in enumerate(cases)]
counts = Counter(r['status'] for r in results)
categories = {}
for r in results:
    categories.setdefault(r['case'].split('.')[1], Counter())[r['status']] += 1
observations = []
for name in batches:
    directory = Path(name)
    report = json.loads((directory/'report.json').read_text())
    guard = json.loads((directory/'remote/result.json').read_text())
    observations.append(dict(evidence=name, verdict=report['verdict'],
        peak_temperature_c=report.get('observations', {}).get('peak_temperature_c'),
        boot_id=guard['boot_id'], watchdog=guard['watchdog'],
        thermal=guard['thermal']))
completed = len(records)
attempted_supported = completed - counts.get('NotSupported', 0)
summary = dict(selected=len(cases), completed=completed,
    counts=dict(counts), rates_percent={k: round(v*100/len(cases), 3) for k, v in counts.items()},
    pass_rate_excluding_unsupported_percent=(round(counts['Pass']*100/attempted_supported, 3)
                                            if attempted_supported else None),
    categories=categories, cases_sha256=hashlib.sha256(raw).hexdigest(),
    segments=[str(s.resolve()) for s in a.segments],
    identities=identities, batches=observations, campaign_timings=timings,
    peak_temperature_c=max((b['peak_temperature_c'] for b in observations
                            if b['peak_temperature_c'] is not None), default=None))
if timings:
    summary['timing'] = dict(
        elapsed_seconds=sum(t['elapsed_seconds'] for t in timings),
        batch_execution_seconds=sum(t['batch_execution_seconds'] for t in timings),
        wall_span_seconds=max(t['finished_unix'] for t in timings) -
                          min(t['started_unix'] for t in timings),
        jobs=sorted(set(t['jobs'] for t in timings)),
        note='Elapsed sums campaign segments including guard/transport overhead; wall span also includes any gaps between segments.')
a.output.mkdir(parents=True, exist_ok=True)
(a.output/'results.json').write_text(json.dumps(results, indent=2)+'\n')
(a.output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
with (a.output/'failures.tsv').open('w', newline='') as stream:
    writer = csv.writer(stream, delimiter='\t')
    writer.writerow(['index', 'status', 'case', 'message'])
    for r in results:
        if r['status'] not in ('Pass', 'NotSupported', 'Unrun'):
            writer.writerow([r['index'], r['status'], r['case'], r.get('message', '')])
lines = [f'# CTS sample: {completed}/{len(cases)} completed', '',
         '| Outcome | Count | % of selected sample |', '| --- | ---: | ---: |']
lines += [f'| {status} | {count} | {count*100/len(cases):.1f}% |'
          for status, count in sorted(counts.items())]
if timings:
    timing = summary['timing']
    lines += ['', f"Concurrent CTS processes: {', '.join(map(str, timing['jobs']))}.",
              f"Campaign elapsed time (including guards/transport): {timing['elapsed_seconds']:.3f} seconds.",
              f"Batch execution wall time: {timing['batch_execution_seconds']:.3f} seconds.",
              f"Overall wall span (including segment gaps): {timing['wall_span_seconds']:.3f} seconds."]
lines += ['', f"Peak recorded temperature: {summary['peak_temperature_c']} C.", '',
          'Unsupported cases are not passes. Timeouts use the recorded per-case bound.',
          'Process crashes are not necessarily GPU resets. Inspect retained logs for attribution.',
          'This sample is not a full Vulkan conformance result.', '']
(a.output/'REPORT.md').write_text('\n'.join(lines))
print('\n'.join(lines))
