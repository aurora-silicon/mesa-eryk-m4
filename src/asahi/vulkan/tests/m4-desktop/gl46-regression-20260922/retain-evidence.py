#!/usr/bin/env python3
"""Mirror compact regression evidence into Mesa without changing outcomes."""
import gzip
import hashlib
import json
from pathlib import Path
import shutil

p = Path(__file__).resolve().parent
root = p.parents[1]
out = root / 'mesa/m4-asahi/src/asahi/vulkan/tests/m4-desktop/gl46-regression-20260922'
out.mkdir(exist_ok=True)
for name in ('plan.json', 'installed-hashes.json', 'environment.json', 'controller.json',
             'controller-stopped-0038.json', 'review-0038.json', 'run-campaign.py',
             'run-remaining.py', 'status.py', 'compare.py', 'compare-final.py',
             'retain-evidence.py', 'corrected-command.json', 'final-comparison.json',
             'kernel-review.json', 'final-target-state.json', 'corrected-depth-coverage.json'):
    shutil.copy2(p / name, out / name)
for name in ('comparison.json', 'latest-with-corrected-cts.json', 'cases.txt',
             'corrected-feature-cases.txt', 'campaign.log', 'remaining.log',
             'corrected-features.log', 'report/summary.json', 'report/results.json',
             'report/latest-coverage.json', 'report/failures.json',
             'corrected-features/summary.json', 'corrected-features/results.json'):
    target = out / (name + '.gz')
    target.parent.mkdir(exist_ok=True, parents=True)
    target.write_bytes(gzip.compress((p / name).read_bytes(), mtime=0))
runs = json.loads((p / 'report/summary.json').read_text())['runs']
runs += [json.loads((p / 'corrected-features/summary.json').read_text())]
manifest = []
for run in runs:
    evidence = Path(run['evidence'])
    target = out / 'guards' / evidence.name
    target.mkdir(parents=True, exist_ok=True)
    for name in ('report.json', 'remote/result.json', 'remote/temperatures.jsonl',
                 'remote/kernel-live.log', 'remote/cts/binary.json',
                 'remote/cts/environment.json'):
        src = evidence / name
        data = src.read_bytes()
        dst = target / (name.replace('/', '-') + '.gz')
        dst.write_bytes(gzip.compress(data, mtime=0))
        manifest.append(dict(source=str(src), sha256=hashlib.sha256(data).hexdigest(),
                             retained=str(dst.relative_to(out))))
failures = {r['case'] for r in json.loads((p / 'report/failures.json').read_text())}
for label, run in (('original', runs[38]), ('corrected', runs[-1])):
    chunks = []
    for qpa in (Path(run['evidence']) / 'remote/cts/deqp-runner').glob('c*.qpa'):
        for chunk in qpa.read_text(errors='replace').split('#beginTestCaseResult ')[1:]:
            if chunk.splitlines()[0].strip() in failures:
                chunks.append('#beginTestCaseResult ' + chunk)
    assert len(chunks) == 6, (label, len(chunks))
    (out / (label + '-depth6.qpa.gz')).write_bytes(gzip.compress(''.join(chunks).encode(), mtime=0))
(out / 'evidence-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
paths = sorted(path for path in out.rglob('*') if path.is_file() and path.name != 'SHA256SUMS')
(out / 'SHA256SUMS').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + str(path.relative_to(out)) + '\n' for path in paths))
print(f'Retained {len(paths)} files in {out}')
