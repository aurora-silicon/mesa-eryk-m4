#!/usr/bin/env python3
"""Intersect vulkaninfo device features/extensions for an explicit reference run.

Does not promise matching format/limit support or hardware equivalence. Retains
both input hashes and every changed feature/extension beside the new profile.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('target', type=Path)
p.add_argument('reference', type=Path)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--registry', type=Path, default=Path(__file__).resolve().parent.parent / 'reference/gfxreconstruct/external/Vulkan-Headers/registry/vk.xml')
p.add_argument('--exclude-extension', action='append', default=[], help='Explicit application-specific restriction, retained in report')
p.add_argument('--formats', action='store_true', help='Also intersect per-format feature flags, including explicit unsupported formats')
a = p.parse_args()
target, reference = [json.loads(f.read_text()) for f in (a.target, a.reference)]
t, r = [d['capabilities']['device'] for d in (target, reference)]
extensions = {k: min(v, r['extensions'][k]) for k, v in t['extensions'].items() if k in r['extensions']}
for extension in a.exclude_extension:
    if extension not in extensions:
        p.error(f'Excluded extension is not in the intersection: {extension}')
    del extensions[extension]
features = {}
changes = []
registry = ET.parse(a.registry).getroot()
aliases = {node.get('name'): node.get('alias') for node in registry.findall('./types/type') if node.get('alias')}
def canonical(name):
    while name in aliases:
        name = aliases[name]
    return name
allowed = {canonical(node.get('name')) for ext in registry.findall('./extensions/extension')
           if ext.get('name') in extensions for node in ext.findall('./require/type')}
allowed |= {'VkPhysicalDeviceFeatures', 'VkPhysicalDeviceProperties'}
allowed |= {f'VkPhysicalDeviceVulkan1{minor}{kind}' for minor in range(1, 5) for kind in ['Features', 'Properties']}
for struct in sorted(t['features']):
    if canonical(struct) not in allowed:
        continue
    features[struct] = {}
    tf, rf = t['features'].get(struct, {}), r['features'].get(struct, {})
    for field in sorted(tf.keys() | rf.keys()):
        tv, rv = tf.get(field, False), rf.get(field, False)
        if not isinstance(tv, bool) or not isinstance(rv, bool):
            raise ValueError(f'Non-boolean feature: {struct}.{field}')
        features[struct][field] = tv and rv
        if tv != rv:
            changes.append(dict(struct=struct, field=field, target=tv, reference=rv, intersection=False))
profile = copy.deepcopy(next(iter(target['profiles'].values())))
profile['label'] = 'M4 and lavapipe shared feature/extension capture profile'
profile['capabilities'] = ['device']
profile['api-version'] = min((next(iter(d['profiles'].values()))['api-version'] for d in (target, reference)),
                             key=lambda value: tuple(map(int, value.split('.'))))
# Keep software-device limits and layout arrays; do not impersonate M4 limits.
properties = {k: v for k, v in r['properties'].items() if canonical(k) in allowed}
result = {'$schema': target['$schema'], 'capabilities': {'device': {'features': features, 'extensions': extensions, 'properties': properties}},
          'profiles': {'VP_M4_reference_intersection': profile}}
if a.formats:
    formats = {}
    for name in sorted(t['formats'].keys() | r['formats'].keys()):
        tf = t['formats'].get(name, {}).get('VkFormatProperties', {})
        rf = r['formats'].get(name, {}).get('VkFormatProperties', {})
        formats[name] = {'VkFormatProperties': {field: sorted(set(tf.get(field, [])) & set(rf.get(field, [])))
            for field in ('linearTilingFeatures', 'optimalTilingFeatures', 'bufferFeatures')}}
    result['capabilities']['device']['formats'] = formats
with a.output.open('x') as f:
    json.dump(result, f, indent=2); f.write('\n')
report = dict(inputs={str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in (a.target, a.reference, a.registry)},
              explicit_exclusions=a.exclude_extension,
              omitted_feature_structures=sorted(t['features'].keys() - features.keys()),
              removed_target_extensions=sorted(t['extensions'].keys() - extensions.keys()),
              removed_reference_extensions=sorted(r['extensions'].keys() - extensions.keys()),
              changed_features=changes,
              formats_intersected=a.formats,
              note='Explicit shared subset; not an unmodified M4 application-support claim. Software limits retained. Format simulation requires SIMULATE_FORMATS_BIT.')
a.output.with_suffix('.report.json').write_text(json.dumps(report, indent=2)+'\n')
print(f'Shared extensions: {len(extensions)}; differing feature fields: {len(changes)}')
