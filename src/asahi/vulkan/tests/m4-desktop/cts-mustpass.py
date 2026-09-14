#!/usr/bin/env python3
"""Expand a CTS mustpass include tree offline, retaining each input hash."""
import argparse
import hashlib
import json
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('mustpass', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = a.mustpass.resolve().parent
inputs = {}
cases = []
active = set()


def expand(path):
    path = path.resolve()
    relative = str(path.relative_to(root))
    if path in active:
        raise ValueError(f'Include cycle: {path}')
    active.add(path)
    raw = path.read_bytes()
    inputs[relative] = hashlib.sha256(raw).hexdigest()
    for line in raw.decode().splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        if re.fullmatch(r'dEQP-VK\.[\w.\-]+', line):
            cases.append(line)
        elif line.endswith('.txt'):
            expand(path.parent / line)
        else:
            raise ValueError(f'Unknown mustpass entry in {path}: {line}')
    active.remove(path)


expand(a.mustpass)
if not cases or len(cases) != len(set(cases)):
    raise ValueError('Expected nonempty unique case names')
a.output.mkdir(parents=True, exist_ok=False)
raw = ('\n'.join(cases) + '\n').encode()
(a.output / 'mustpass.txt').write_bytes(raw)
record = dict(source=str(a.mustpass.resolve()), inputs=inputs, count=len(cases),
              sha256=hashlib.sha256(raw).hexdigest(),
              method='Recursive in-order expansion; no capability or result filtering')
(a.output / 'mustpass.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps({k: v for k, v in record.items() if k != 'inputs'}, indent=2))
