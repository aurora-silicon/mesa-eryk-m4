#!/usr/bin/env python3
"""Resolve top-of-stack FEX block samples against explicit guest ELF binaries.

Example: fex-jit-summary.py menu.perf.script --library libvulkan_asahi-test.so=/path/tested.so
Only the sampled block's guest start address is known, not its exact instruction.
Use the binary that was installed during the sample, not a later rebuild.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('script', type=Path)
p.add_argument('--library', action='append', default=[], metavar='NAME=ELF')
p.add_argument('--addr2line', default='llvm-addr2line')
a = p.parse_args()
binaries = {name: Path(path) for name, path in (entry.split('=', 1) for entry in a.library)}
blocks = Counter()
threads = Counter()
native = Counter()
for block in a.script.read_text().strip().split('\n\n'):
    lines = block.splitlines()
    if len(lines) < 2:
        continue
    threads[lines[0].rsplit(None, 2)[0].strip()] += 1
    sampled = lines[1]
    match = re.search(r' (\S+)\+(0x[0-9a-fA-F]+) \(0x', sampled)
    if match:
        blocks[(Path(match[1]).name, match[2])] += 1
    else:
        native[sampled.split(' ', 1)[-1].strip()] += 1
resolved = {}
for name, elf in binaries.items():
    addresses = sorted({address for library, address in blocks if library == name})
    if not addresses:
        continue
    output = subprocess.check_output([a.addr2line, '-fe', str(elf), *addresses], text=True).splitlines()
    if len(output) != 2 * len(addresses):
        raise RuntimeError('Unexpected addr2line output')
    counts = Counter()
    for i, address in enumerate(addresses):
        counts[output[2*i]] += blocks[name, address]
    resolved[name] = dict(elf=str(elf.resolve()), sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                          functions=counts.most_common())
print(json.dumps(dict(scope='CPU samples attributed to guest block starts; not GPU utilization',
    jit_samples=sum(blocks.values()), native_samples=sum(native.values()),
    libraries=Counter({name:sum(n for (lib, _),n in blocks.items() if lib == name)
                       for name, _ in blocks}).most_common(),
    resolved=resolved, native_top=native.most_common(15)), indent=2))
