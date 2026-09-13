#!/usr/bin/env python3
"""Extract PNG images and compact pixel statistics from one CTS QPA.

Usage: tools/cts-images.py result.qpa --output NEW_DIRECTORY
Offline only. Retains exact embedded PNGs, source hash and comparison counts.
Requires Pillow; does not launch a renderer or alter test expectations.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET
from PIL import Image

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('qpa', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
raw = a.qpa.read_bytes()
xml = raw[raw.index(b'<?xml'):raw.index(b'</TestCaseResult>') + len(b'</TestCaseResult>')]
root = ET.fromstring(xml)
report = {'qpa': str(a.qpa.resolve()), 'sha256': hashlib.sha256(raw).hexdigest(), 'sets': []}
for n, group in enumerate(root.iter('ImageSet')):
    record = {'name': group.get('Name'), 'images': []}
    pixels = {}
    for i, node in enumerate(group.findall('Image')):
        if node.get('CompressionMode') != 'PNG':
            raise ValueError('Unsupported embedded image compression')
        data = base64.b64decode(''.join(node.itertext()))
        file = a.output / f'{n}-{i}.png'
        file.write_bytes(data)
        im = Image.open(file).convert('RGBA')
        px = list(im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata())
        pixels[node.get('Name')] = px
        colors = im.getcolors(im.width * im.height)
        record['images'].append({'name': node.get('Name'), 'file': file.name,
            'size': im.size, 'sha256': hashlib.sha256(data).hexdigest(),
            'unique_colors': len(colors), 'most_common': sorted(colors, reverse=True)[:5]})
    if 'Result' in pixels and 'Reference' in pixels:
        x, y = pixels['Result'], pixels['Reference']
        if len(x) != len(y):
            raise ValueError('Image sizes differ')
        record['unequal_pixels'] = sum(u != v for u, v in zip(x, y))
        record['total_pixels'] = len(x)
    report['sets'].append(record)
(a.output / 'images.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
