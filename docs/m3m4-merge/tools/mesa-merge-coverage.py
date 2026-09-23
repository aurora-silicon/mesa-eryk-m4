#!/usr/bin/env python3
"""Compare indexed outcomes from identical frozen catalogs, without rewriting them.

Unrecorded/unsupported cases remain explicit. A baseline repair is reported
separately rather than silently replacing the original result. New failures
and loss of an originally passing capability require review.
"""
import argparse,collections,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path)
p.add_argument('--output',type=Path)
a=p.parse_args()
old=(a.baseline/'catalog.json').read_bytes();new=(a.candidate/'catalog.json').read_bytes()
assert old==new,'Catalog identity differs'
base={r['global_index']:r for r in json.loads((a.baseline/'results.json').read_text())}
rows=json.loads((a.candidate/'results.json').read_text())
transitions=collections.Counter();review=[];unmatched=0
for r in rows:
 b=base.get(r['global_index'])
 if b:
  assert b['case']==r['case'] and b['session']==r['session']
  transitions[b['status']+' -> '+r['status']]+=1
 else:unmatched+=1
 if r['status'] in ('Fail','Crash','Timeout','HarnessError','ResourceError','InternalError','GPUTimeout') or (b and b['status']=='Pass' and r['status'] in ('NotSupported','QualityWarning','CompatibilityWarning')):
  review.append(dict(global_index=r['global_index'],case=r['case'],baseline=b['status'] if b else None,candidate=r['status'],evidence=r.get('evidence',r.get('run'))))
report=dict(catalog_sha256=hashlib.sha256(old).hexdigest(),candidate_records=len(rows),catalog_total=json.loads(new)['total'],counts=dict(collections.Counter(r['status'] for r in rows)),unmatched_baseline=unmatched,transitions=dict(transitions),review=review)
if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
