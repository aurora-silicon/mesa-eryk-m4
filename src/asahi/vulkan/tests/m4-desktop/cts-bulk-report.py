#!/usr/bin/env python3
"""Combine inspected bulk CTS continuations without overwriting completed outcomes.

Offline only. Re-read retained QPA/stdout with the current collector so canceled
workers are not counted as independent crashes. Retain original segment reports.
"""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path

from cts_deqp_backend import collect

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cases', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('segments', type=Path, nargs='+')
a = p.parse_args()
raw = a.cases.read_bytes()
cases = raw.decode().splitlines()
if len(cases) != len(set(cases)):
    raise ValueError('Duplicate frozen case')
ledger = {c: [] for c in cases}
terminal = {'Pass', 'Fail', 'Crash', 'Timeout', 'NotSupported', 'QualityWarning', 'CompatibilityWarning', 'InternalError', 'ResourceError', 'HarnessError'}
segments, identity = [], None
for source in a.segments:
    summary = json.loads((source/'summary.json').read_text())
    evidence = Path(summary['evidence'])
    before = json.loads((evidence/'target-before.json').read_text())
    out = evidence/'remote/cts'
    fixed_files = {k:v for k,v in before['files'].items() if not k.startswith('/root/cts-bulk/')}
    staged_code = sorted((Path(k).name,v) for k,v in before['files'].items()
                         if k.startswith('/root/cts-bulk/') and Path(k).name != 'cases.txt')
    software = [before['kernel'], fixed_files, staged_code,
                json.loads((out/'binary.json').read_text()),
                json.loads((out/'runner.json').read_text())['sha256']]
    if identity is not None and software != identity:
        raise ValueError('Driver, CTS, runner or guard changed between segments')
    identity = software
    phase_raw = (out/'runner-cases.txt').read_bytes()
    phase_cases = phase_raw.decode().splitlines()
    meta_path = out/'selection-meta.json'
    meta = json.loads(meta_path.read_text()) if meta_path.exists() else None
    if meta and (meta['cases_sha256'] != hashlib.sha256(phase_raw).hexdigest() or
                 [r['case'] for r in meta['cases']] != phase_cases):
        raise ValueError('Frozen metadata/list mismatch')
    rows = collect(out, phase_cases, meta, 0, before['boot_id'])
    for r in rows:
        case = r['case']
        if case not in ledger:
            raise ValueError(f'Case outside frozen selection: {case}')
        if r['status'] == 'Unrun':
            continue
        history = ledger[case]
        if history and history[-1]['status'] in terminal:
            raise ValueError(f'Completed outcome retried: {case}')
        history.append(dict(segment=len(segments), status=r['status'],
            classification=r['eligibility']['classification'],
            message=r.get('message'), runner_status=r['runner_status'],
            worker_exit=r.get('worker_exit'), qpa=r.get('qpa'), seconds=r.get('seconds')))
    timing = json.loads((out/'timing.json').read_text())
    segments.append(dict(directory=str(source.resolve()), evidence=str(evidence),
        boot_id=before['boot_id'], counts=Counter(r['status'] for r in rows),
        jobs=summary['jobs'], elapsed_seconds=summary['elapsed_seconds'],
        preparation_seconds=summary['preparation_seconds'],
        runner_seconds=timing['elapsed_seconds'], started_unix=timing['started_unix'],
        finished_unix=timing['finished_unix'], guard_verdict=summary['guard_verdict'],
        peak_temperature_c=summary['peak_temperature_c']))
results = [dict(index=i, case=c, status=ledger[c][-1]['status'] if ledger[c] else 'Unrun',
                classification=ledger[c][-1]['classification'] if ledger[c] else 'unclassified',
                attempts=ledger[c]) for i, c in enumerate(cases)]
counts = Counter(r['status'] for r in results)
report = dict(selected=len(cases), completed=sum(r['status'] in terminal for r in results),
    counts=counts, rates_percent={k: round(v*100/len(cases), 3) for k,v in counts.items()},
    classifications=Counter(r['classification'] for r in results),
    observed_attempts=sum(len(r['attempts']) for r in results),
    restarted_interrupted_attempts=sum(max(0,len(r['attempts'])-1) for r in results),
    first_attempt_counts=Counter(r['attempts'][0]['status'] if r['attempts'] else 'Unrun' for r in results),
    runner_seconds=sum(s['runner_seconds'] for s in segments),
    active_campaign_seconds=sum(s['elapsed_seconds'] for s in segments),
    staging_seconds=sum(s['preparation_seconds'] for s in segments),
    wall_span_seconds=segments[-1]['finished_unix']-segments[0]['started_unix'],
    peak_temperature_c=max(s['peak_temperature_c'] for s in segments),
    cases_sha256=hashlib.sha256(raw).hexdigest(), software_identity=identity, segments=segments)
a.output.mkdir(parents=True,exist_ok=False)
(a.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
(a.output/'cases.txt.gz').write_bytes(gzip.compress(raw,mtime=0))
(a.output/'results.jsonl.gz').write_bytes(gzip.compress(('\n'.join(json.dumps(r) for r in results)+'\n').encode(),mtime=0))
with (a.output/'failures.tsv').open('w') as f:
    f.write('status\tclassification\tcase\tmessage\n')
    for r in results:
        if r['status'] in ('Pass','NotSupported'):
            continue
        message=r['attempts'][-1].get('message','') if r['attempts'] else ''
        f.write('\t'.join(str(x).replace('\n',' ').replace('\t',' ') for x in
                         [r['status'],r['classification'],r['case'],message])+'\n')
lines=[f'# CTS: {len(cases):,} frozen cases', '',
       '| Result | Count | % |','| --- | ---: | ---: |']
lines += [f'| {k} | {v:,} | {v*100/len(cases):.3f} |' for k,v in counts.items()]
lines += ['',f"Runner execution: {report['runner_seconds']:.3f} seconds.",
          f"Active campaign time including guard/transfer: {report['active_campaign_seconds']:.3f} seconds.",
          f"Staging: {report['staging_seconds']:.3f} seconds.",
          f"Wall span between first start and last result, including inspection pauses: {report['wall_span_seconds']:.3f} seconds.",
          f"Peak temperature: {report['peak_temperature_c']} C.", '',
          f"{report['restarted_interrupted_attempts']} interrupted attempts restarted; no completed Pass/Fail/Crash/Timeout was retried or replaced.",
          'Workers per segment: ' + ', '.join(str(s['jobs']) for s in segments) + '.',
          ('This was an interrupted and resumed campaign, not one uninterrupted throughput benchmark.'
           if len(segments) > 1 else 'One bounded execution segment.'),
          'Raw runner crashes caused by coordinated SIGKILL are classified as Interrupted; true SIGSEGV and timeout results remain failures.',
          'Exact first-attempt and segment counts are in summary.json; every attempt is retained in results.jsonl.gz.',
          'The exact CTS executable and driver identities are retained in summary.json. This random sample is not a Khronos conformance submission.', '']
(a.output/'README.md').write_text('\n'.join(lines))
print(json.dumps({k:v for k,v in report.items() if k not in ('software_identity','segments')},indent=2))
