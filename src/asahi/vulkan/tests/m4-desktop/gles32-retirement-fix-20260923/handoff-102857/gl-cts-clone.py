#!/usr/bin/env python3
"""Clone only a frozen GL CTS catalog for a new pinned driver experiment.

Usage: python3 tools/gl-cts-clone.py SOURCE_CAMPAIGN NEW_CAMPAIGN

Copies the plan, environment, session case lists and indexed segment inputs.
No outcomes, recovery reviews or installed-driver hashes are inherited.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
p.add_argument('destination', type=Path)
a = p.parse_args()
source = a.source.resolve(strict=True)
destination = a.destination.resolve()
assert not destination.exists(), destination
catalog_bytes = (source / 'catalog.json').read_bytes()
catalog = json.loads(catalog_bytes)
assert hashlib.sha256((source / 'plan.xml').read_bytes()).hexdigest() == catalog['plan_sha256']
assert sum(s['count'] for s in catalog['sessions']) == catalog['total']
assert sum(s['count'] for s in catalog['segments']) == catalog['total']

def copy(relative):
    old = source / relative
    new = destination / relative
    new.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(old, new)

for name in ('catalog.json', 'plan.xml', 'environment.json'):
    copy(Path(name))
for session in catalog['sessions']:
    relative = Path('sessions') / f"{session['index']:03d}" / 'cases.txt'
    contents = (source / relative).read_bytes()
    assert hashlib.sha256(contents).hexdigest() == session['sha256']
    assert len(contents.splitlines()) == session['count']
    copy(relative)
for segment in catalog['segments']:
    folder = Path('segments') / f"{segment['index']:04d}"
    contents = (source / folder / 'cases.txt').read_bytes()
    assert hashlib.sha256(contents).hexdigest() == segment['sha256']
    meta = json.loads((source / folder / 'cases.meta.json').read_text())
    assert meta['cases_sha256'] == segment['sha256']
    assert [row['catalog_index'] for row in meta['cases']] == list(range(segment['start'], segment['start'] + segment['count']))
    copy(folder / 'cases.txt')
    copy(folder / 'cases.meta.json')
(destination / 'clone.json').write_text(json.dumps({
    'source': str(source),
    'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'catalog_sha256': hashlib.sha256(catalog_bytes).hexdigest(),
    'plan_sha256': catalog['plan_sha256'],
    'total': catalog['total'],
    'sessions': len(catalog['sessions']),
    'segments': len(catalog['segments']),
    'inherits_outcomes': False,
}, indent=2) + '\n')
print(destination / 'clone.json')
