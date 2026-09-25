#!/usr/bin/env python3
"""Check cache UUID isolation under an existing watchdog/thermal guard.

Arguments: EVIDENCE_DIR MESA_PREFIX [--expect-collision]. Install the companion
cache-options-probe executable at /root/gpu-replay-bench/bin/ first.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

out, prefix = map(Path, sys.argv[1:3])
assert any(n in Path('/proc', str(os.getppid()), 'cmdline').read_text()
           for n in ('guard.py', 'guarded-command.py'))
guard = json.loads((out / ('result.json' if (out / 'result.json').exists()
                          else 'start.json')).read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds'] == 60
binary = Path('/root/gpu-replay-bench/bin/cache-options-probe')
env = {k: v for k, v in os.environ.items()
       if not k.startswith(('VK_', 'HK_', 'AGX_', 'ASAHI_', 'MESA_', 'LD_'))}
env.update(VK_DRIVER_FILES=str(prefix / 'asahi.json'),
           LD_LIBRARY_PATH=str(prefix / 'lib'),
           ASAHI_M3_EXPERIMENTAL='1', ASAHI_M4_EXPERIMENTAL='1')
configs = {}
for name, value in [('disabled', 'true'), ('enabled', 'false')]:
    config = out / name
    config.mkdir()
    (config / '00-options.conf').write_text(
        '<driconf><device driver="hk"><engine engine_name_match="vkd3d">'
        f'<option name="hk_disable_border_emulation" value="{value}" />'
        '</engine></device></driconf>\n')
    configs[name] = config
results = {}
for name, config, perftest in [('disabled', 'disabled', ''),
                              ('enabled', 'enabled', ''),
                              ('perftest', 'enabled', 'noborder'),
                              ('repeat', 'disabled', '')]:
    text = subprocess.check_output([str(binary)], text=True, timeout=8,
                                  env=dict(env, DRIRC_CONFIGDIR=str(configs[config]),
                                           HK_PERFTEST=perftest))
    results[name] = json.loads(text)
assert results['repeat'] == results['disabled']
collision = results['enabled'] == results['disabled'] == results['perftest']
expected_collision = '--expect-collision' in sys.argv[3:]
files = [binary, prefix / 'lib/libvulkan_asahi.so']
record = dict(results=results, collision=collision,
              sha256={str(p): hashlib.file_digest(p.open('rb'), 'sha256').hexdigest()
                      for p in files})
(out / 'cache-options.json').write_text(json.dumps(record, indent=2) + '\n')
if expected_collision:
    assert collision
else:
    for key in ('pipeline', 'shader'):
        assert len({results[n][key] for n in ('disabled', 'enabled', 'perftest')}) == 3
print('CACHE_OPTIONS_EXPECTATION_PASS', json.dumps(record), flush=True)
