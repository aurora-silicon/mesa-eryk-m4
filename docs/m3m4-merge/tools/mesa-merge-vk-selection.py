#!/usr/bin/env python3
"""Freeze deterministic Vulkan merge regressions without filtering capabilities/results.

Run from the workspace root with a NEW output directory. Keeps global indices
from the pinned Vulkan 1.4 catalog, every retained M3 repair case, all native
LOD queries, and evenly spaced witnesses from the merge's other risk families.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
cat = root/'build/cts-vulkan14-ordered/catalog'
raw = (cat/'catalog.txt').read_bytes()
identity = json.loads((cat/'catalog.json').read_text())
assert hashlib.sha256(raw).hexdigest() == identity['catalog_sha256']
cases = raw.decode().splitlines()
indices = {c:i for i,c in enumerate(cases)}
selected = {}
def add(case, reason):
    selected.setdefault(indices[case], []).append(reason)
for path in sorted((root/'mesa/m3m4-merged/src/asahi/vulkan/tests/m3-cts').glob('*/cases.txt')):
    for c in path.read_text().splitlines():
        add(c, 'retained M3 regression: '+path.parent.name)
patterns = {
    'LOD query': (r'^dEQP-VK.glsl.texture_functions.query.texturequerylod\.', 0),
    'gather and border': (r'^dEQP-VK.*gather.*(border|offset)', 12),
    'sampler reduction': (r'^dEQP-VK.pipeline.monolithic.sampler.*reduce.*\.(min|max)(_|$)', 8),
    'dynamic UBO': (r'^dEQP-VK.binding_model.dynamic_offset\.', 12),
    'pipeline cache': (r'^dEQP-VK.pipeline.*\.cache\.', 8),
    'pipeline binary': (r'^dEQP-VK.pipeline.*pipeline_binary', 8),
    'command reuse': (r'^dEQP-VK.api.command_buffers\.', 12),
    'mixed synchronization': (r'^dEQP-VK.synchronization.*(compute.*draw|draw.*compute)', 12),
    'timeline': (r'^dEQP-VK.synchronization.basic.timeline_semaphore\.', 8),
    'resolve': (r'^dEQP-VK.api.copy_and_blit.*resolve_image', 8),
    'geometry': (r'^dEQP-VK.geometry\.', 8),
    'tessellation': (r'^dEQP-VK.tessellation\.', 8),
    'sparse': (r'^dEQP-VK.sparse_resources\.', 8),
    'buffer address': (r'^dEQP-VK.*buffer_device_address', 8),
}
manifest = {}
for reason, (pattern, count) in patterns.items():
    rows = [c for c in cases if re.search(pattern,c)]
    assert rows, reason
    chosen = rows if not count or len(rows)<=count else [rows[i*(len(rows)-1)//(count-1)] for i in range(count)]
    manifest[reason] = dict(pattern=pattern, matched=len(rows), selected=len(chosen), method='all' if not count else 'evenly spaced in frozen order')
    for c in chosen:
        add(c, reason)
a.output.mkdir(parents=True, exist_ok=False)
rows = [dict(case=cases[i], catalog_index=i, reasons=selected[i]) for i in sorted(selected)]
raw = ('\n'.join(r['case'] for r in rows)+'\n').encode()
(a.output/'cases.txt').write_bytes(raw)
(a.output/'cases.meta.json').write_text(json.dumps(dict(schema=1, cases_sha256=hashlib.sha256(raw).hexdigest(), cases=rows),indent=2)+'\n')
(a.output/'selection.json').write_text(json.dumps(dict(catalog=identity,groups=manifest,count=len(rows)),indent=2)+'\n')
print(len(rows))
