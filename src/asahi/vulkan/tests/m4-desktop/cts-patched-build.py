#!/usr/bin/env python3
"""Build a separately identified CTS diagnostic binary from a trusted patch.

Restores the original source files and executable even on build failure. Run
without another CTS build in progress. Does not install or execute GPU tests;
use gpu-experiment.py for hardware execution and keep adapted results separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('patch', type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--source', type=Path, default=ROOT/'reference/vk-gl-cts-1.0.2.6')
p.add_argument('--build', type=Path, default=ROOT/'build/cts-1.0-arm64')
a = p.parse_args()
patch = a.patch.resolve()
source = a.source.resolve()
binary = a.build.resolve()/'external/vulkancts/modules/vulkan/deqp-vk'
output = a.output.resolve()
subprocess.run(['git', 'apply', '--check', str(patch)], cwd=source, check=True)
stats = subprocess.check_output(['git', 'apply', '--numstat', str(patch)],
                                cwd=source, text=True)
originals = {}
for line in stats.splitlines():
    name = line.split('\t', 2)[2]
    path = (source/name).resolve()
    if not path.is_relative_to(source) or not path.is_file():
        raise ValueError('Only patches to existing source files are supported')
    originals[path] = path.read_bytes()
output.mkdir(parents=True, exist_ok=False)
shutil.copy2(patch, output/'diagnostic.patch')
shutil.copy2(binary, output/'deqp-vk.original')
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
record = dict(source=str(source),
              source_commit=subprocess.check_output(
                  ['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip(),
              patch_sha256=sha(patch), original_binary_sha256=sha(binary),
              source_sha256={str(k.relative_to(source)): hashlib.sha256(v).hexdigest()
                             for k, v in originals.items()})
try:
    subprocess.run(['git', 'apply', str(patch)], cwd=source, check=True)
    with (output/'build.log').open('w') as log:
        subprocess.run(['ninja', '-C', str(a.build.resolve()), '-j12', 'deqp-vk'],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    shutil.copy2(binary, output/'deqp-vk')
    record['diagnostic_binary_sha256'] = sha(output/'deqp-vk')
finally:
    for path, data in originals.items():
        path.write_bytes(data)
    shutil.copy2(output/'deqp-vk.original', binary)
    record['restored'] = (sha(binary) == record['original_binary_sha256'] and
                          all(k.read_bytes() == v for k, v in originals.items()))
    (output/'manifest.json').write_text(json.dumps(record, indent=2)+'\n')
print(json.dumps(record, indent=2))
