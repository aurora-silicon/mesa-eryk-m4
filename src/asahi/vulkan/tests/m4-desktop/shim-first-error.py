#!/usr/bin/env python3
"""Locate the first Vulkan error in an existing offline fragment probe.

Only accepts vulkan-fragment-probe.py manifests for host-shim recording or
pipeline compilation. This never runs an ARM binary or a hardware driver.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('probe', type=Path)
args = p.parse_args()
directory = args.probe.resolve()
m = json.loads((directory / 'manifest.json').read_text())
env = m.get('host_environment', {})
binary = directory / 'probe-host-compile-only'
if not m.get('host_compile_only') or env.get('ASAHI_SHIM_CHIP') != 'g16g':
    p.error('requires a retained M4 host-shim probe manifest')
if hashlib.sha256(binary.read_bytes()).hexdigest() != m['sha256'][binary.name]:
    p.error('probe executable changed since its manifest was written')
for name in ('LD_PRELOAD', 'VK_DRIVER_FILES'):
    if '\n' in env[name] or not Path(env[name]).is_file():
        p.error('invalid retained host-shim environment')
commands = ['set debuginfod enabled off', 'set breakpoint pending on']
for k, v in env.items():
    if '\n' in k + v:
        p.error('multiline environment value')
    commands.append(f'set environment {k} {v}')
# Break at the non-inlined logger: optimized inline error helpers also map
# successful branches to the same line and produce misleading backtraces.
commands += ['break __vk_errorf', 'run', 'bt 12', 'info args']
script = directory / 'first-error.gdb'
script.write_text('\n'.join(commands) + '\n')
r = subprocess.run(['gdb', '-q', '-batch', '-x', str(script), str(binary)],
                   capture_output=True, text=True, timeout=30)
log = directory / 'first-error.log'
log.write_text(r.stdout + r.stderr)
print(r.stdout[-6000:])
print(log)
