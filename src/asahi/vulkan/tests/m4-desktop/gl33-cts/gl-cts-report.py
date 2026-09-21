#!/usr/bin/env python3
"""Aggregate guarded OpenGL CTS runs without hiding skips or failed attempts.
Usage: gl-cts-report.py --output REPORT_DIR RUN_DIR [RUN_DIR ...]
"""
import argparse
from collections import Counter
import json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--catalog',type=Path,help='Frozen ordered catalog for cross-segment case indices')
p.add_argument('runs',type=Path,nargs='+')
a=p.parse_args(); a.output.mkdir(parents=True,exist_ok=True)
records=[]; runs=[]
indices={r['case']:r['index'] for r in json.loads(a.catalog.read_text())['cases']} if a.catalog else {}
for run in a.runs:
 summary=json.loads((run/'summary.json').read_text())
 runs.append(dict(run=str(run),**summary))
 for r in json.loads((run/'results.json').read_text()):
  records.append(dict(run=str(run),catalog_index=indices.get(r['case']),**r))
failures=[r for r in records if r['status'] not in ('Pass','NotSupported','QualityWarning','CompatibilityWarning','Unrun','Interrupted')]
summary=dict(counts=dict(Counter(r['status'] for r in records)),runs=runs,
 failing_cases=len({r['case'] for r in failures}),
 limitation='Development CTS results; configurations and repeated attempts remain separate, not certification.')
if indices:
 coverage=[]
 for case,index in indices.items():
  attempts=[r for r in records if r['case']==case and r['status'] not in ('Unrun','Interrupted')]
  coverage.append(dict(index=index,case=case,status=attempts[0]['status'] if len(attempts)==1 else ('Unrun' if not attempts else 'MultipleAttempts'),attempts=attempts))
 summary['coverage_counts']=dict(Counter(r['status'] for r in coverage))
 (a.output/'coverage.json').write_text(json.dumps(coverage,indent=2)+'\n')
(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
(a.output/'results.json').write_text(json.dumps(records,indent=2)+'\n')
(a.output/'failures.json').write_text(json.dumps(failures,indent=2)+'\n')
(a.output/'failures.txt').write_text(''.join(c+'\n' for c in sorted({r['case'] for r in failures})))
(a.output/'nonpass.tsv').write_text('status\tcase\trun\n'+''.join(f"{r['status']}\t{r['case']}\t{r['run']}\n" for r in records if r['status']!='Pass'))
print(json.dumps(summary,indent=2))
