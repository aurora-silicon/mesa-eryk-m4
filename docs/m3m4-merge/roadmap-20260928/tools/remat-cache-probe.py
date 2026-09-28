#!/usr/bin/env python3
"""Check compiler debug cache UUID isolation under the existing M4 guard."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

out, prefix = map(Path, sys.argv[1:])
assert 'guarded-command.py' in Path('/proc', str(os.getppid()), 'cmdline').read_text()
state = json.loads((out / 'start.json').read_text())
assert state['watchdog']['armed'] and state['watchdog']['timeout_seconds'] == 60
env = dict(os.environ, VK_DRIVER_FILES=str(prefix / 'asahi.json'),
           LD_LIBRARY_PATH=str(prefix / 'lib'), ASAHI_M4_EXPERIMENTAL='1')
results = {}
for option in ('', 'wait', 'noopt', 'remat', 'remat,shaders', 'shaders'):
    env['AGX_MESA_DEBUG'] = option
    run = subprocess.run(['/usr/bin/vulkaninfo'], env=env, capture_output=True,
                         text=True, timeout=7, check=True)
    name = option or 'normal'
    (out / f'{name}.txt').write_text(run.stdout + run.stderr)
    assert 'Apple M4' in run.stdout
    uuids = re.findall(r'pipelineCacheUUID\s*=\s*(\S+)', run.stdout)
    assert len(set(uuids)) == 1, uuids
    results[name] = uuids[0]
(out / 'compiler-cache-uuids.json').write_text(json.dumps(results, indent=2) + '\n')
assert len({results[k] for k in ('normal', 'wait', 'noopt', 'remat')}) == 4, results
assert results['normal'] == results['shaders'], results
assert results['remat'] == results['remat,shaders'], results
print('COMPILER_CACHE_UUID_ISOLATION_PASS', flush=True)
