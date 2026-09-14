#!/usr/bin/env python3
"""Audit CTS skips offline using exact reasons and explicit source-based reviews.

Preserves raw statuses. Unreviewed skips never become evidence of optionality.
The registry inventory covers unconditional core feature requirements only;
conditional features, formats and limits still require manual source review.
"""
import argparse
from collections import Counter, defaultdict
import gzip
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--results', type=Path, required=True)
p.add_argument('--profile', type=Path, required=True)
p.add_argument('--registry', type=Path, required=True)
p.add_argument('--review', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
raw = a.results.read_bytes()
data = gzip.decompress(raw) if a.results.suffix == '.gz' else raw
rows = ([json.loads(line) for line in data.splitlines()] if '.jsonl' in a.results.name
        else json.loads(data))
profile = json.loads(a.profile.read_text())
device = profile['device']
api = device['properties']['VkPhysicalDeviceProperties']['apiVersion']
version = ((api >> 22) & 127, (api >> 12) & 1023)
registry_raw = a.registry.read_bytes()
registry = ET.fromstring(registry_raw)
# vulkaninfo reports promoted features in the Vulkan11/12/13/14 aggregates,
# while registry requirements can name the original feature structure.
aggregates = {}
for struct, fields in device['features'].items():
    if re.fullmatch(r'VkPhysicalDeviceVulkan\d+Features', struct):
        for name, value in fields.items():
            if name in aggregates and aggregates[name][1] != value:
                raise ValueError('Conflicting promoted feature values: ' + name)
            aggregates[name] = (struct, value)
requirements, gaps = [], []
for core in registry.findall('feature'):
    if ('vulkan' not in core.get('api', '').split(',') or
            tuple(map(int, core.get('number', '9.9').split('.'))) > version):
        continue
    for req in core.findall('require'):
        if req.get('depends'):
            continue
        for feature in req.findall('feature'):
            if feature.get('depends'):
                continue
            name, struct = feature.get('name'), feature.get('struct')
            value = device['features'].get(struct, {}).get(name)
            profile_struct = struct
            if value is None and name in aggregates:
                profile_struct, value = aggregates[name]
            record = dict(feature=name, struct=struct, profile_struct=profile_struct,
                          core=core.get('name'), advertised=value)
            requirements.append(record)
            if value is False:
                gaps.append(record)
reviews = json.loads(a.review.read_text()) if a.review else []
for rule in reviews:
    if rule['classification'] not in ('NotSupported', 'NotSupportedButShouldBeSupported') or not rule['basis']:
        raise ValueError('Each review needs a valid classification and source-based rationale')
groups = defaultdict(list)
results = []
for row in rows:
    latest = row['attempts'][-1] if row.get('attempts') else row
    original = row['status']
    message = latest.get('message', '')
    result = dict(case=row['case'], cts_status=original, status=original, message=message)
    if original == 'NotSupported':
        matches = [r for r in reviews if r['message'] == message and
                   re.fullmatch(r['case_regex'], row['case'])]
        if len(matches) > 1:
            raise ValueError('Overlapping reviews for ' + row['case'])
        if matches:
            result.update(status=matches[0]['classification'], review=matches[0])
        else:
            result.update(status='NotSupportedNeedsReview')
            groups[message].append(row['case'])
    results.append(result)
a.output.mkdir(parents=True, exist_ok=False)
summary = dict(counts=Counter(r['status'] for r in results),
               original_counts=Counter(r['cts_status'] for r in results),
               rates_percent={k: round(v * 100 / len(results), 3)
                              for k, v in Counter(r['status'] for r in results).items()},
               api_version='.'.join(map(str, version)),
               unconditional_core_feature_gaps=gaps,
               inputs={str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                       for path in [a.results, a.profile, a.registry] + ([a.review] if a.review else [])},
               coverage='Registry inventory excludes conditional requirements, formats and limits. Case classification uses explicit reviews, not substring guesses.')
(a.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
(a.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
(a.output / 'core-feature-requirements.json').write_text(json.dumps(requirements, indent=2) + '\n')
(a.output / 'unreviewed.json').write_text(json.dumps(dict(groups), indent=2) + '\n')
print(json.dumps(summary, indent=2))
