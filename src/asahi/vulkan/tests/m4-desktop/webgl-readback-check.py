#!/usr/bin/env python3
"""Check saved 640x480 RGBA readbacks from webgl-readback.html; no GPU execution."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory', type=Path, help='gl-readback directory retained by firefox-probe')
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()
files = sorted(a.directory.glob('*-640x480-1908.bin'))
if not files:
    p.error('no full-size GL_RGBA readbacks found')
y, x = np.indices((480, 640))
expected = np.stack((np.where(x < 320, 191, 32), np.where(y < 240, 64, 128),
                     np.full_like(x, 16), np.full_like(x, 255)), axis=-1).astype(np.uint8)
report = {'expected_sha256': hashlib.sha256(expected.tobytes()).hexdigest(), 'readbacks': []}
for path in files:
    raw = path.read_bytes()
    if len(raw) != expected.size:
        p.error(f'wrong byte count: {path}')
    actual = np.frombuffer(raw, dtype=np.uint8).reshape(expected.shape)
    error = np.abs(actual.astype(np.int16) - expected)
    report['readbacks'].append({'path': str(path), 'sha256': hashlib.sha256(raw).hexdigest(),
        'bad_pixels': int(np.any(error > 1, axis=2).sum()), 'maximum_channel_error': int(error.max())})
report['pass'] = all(r['bad_pixels'] == 0 for r in report['readbacks'])
with a.output.open('x') as out:
    json.dump(report, out, indent=2); out.write('\n')
print(json.dumps({'pass': report['pass'], 'readbacks': len(files),
                  'bad_pixels': sum(r['bad_pixels'] for r in report['readbacks'])}))
raise SystemExit(0 if report['pass'] else 1)
