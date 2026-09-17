#!/usr/bin/env python3
"""Compile and exercise the real Mesa BO cache without a GPU or DRM device."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('host_build', type=Path, help='Configured native Mesa build')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
build = a.host_build.resolve(strict=True)
output = a.output.resolve()
output.mkdir(parents=True, exist_ok=False)
commands = json.loads((build / 'compile_commands.json').read_text())
entry = next(e for e in commands if e['file'].endswith('/agx_bo.c'))
argv = shlex.split(entry['command'])
flags = []
i = 0
while i < len(argv):
    arg = argv[i]
    if arg in ('-o', '-MF', '-MQ', '-MT'):
        i += 2
        continue
    if arg == '-c' or arg in ('-MD', '-MMD') or arg == entry['file']:
        i += 1
        continue
    flags.append(arg)
    i += 1
binary = output / 'bo-cache-test'
command = flags + ['-UNDEBUG', '-ffunction-sections', '-fdata-sections',
    str((build / entry['file']).resolve().parent / 'tests/test-bo-cache.c'),
    *[str((build / entry['file']).resolve().parents[2] / f)
      for f in ('util/futex.c', 'util/os_time.c', 'c11/impl/time.c')],
    '-Wl,--gc-sections', '-pthread', '-o', str(binary)]
(output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
with (output / 'build.log').open('w') as log:
    built = subprocess.run(command, cwd=build, stdout=log, stderr=subprocess.STDOUT,
                           timeout=60)
if built.returncode:
    raise SystemExit(f'Compilation failed; see {output / "build.log"}')
result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
(output / 'result.log').write_text(result.stdout + result.stderr)
print(result.stdout + result.stderr, end='')
result.check_returncode()
