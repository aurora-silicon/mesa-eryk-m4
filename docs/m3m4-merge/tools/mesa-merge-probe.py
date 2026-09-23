#!/usr/bin/env python3
"""Target-side merge probe, invoked only inside the target's existing guard.

Usage: mesa-merge-probe.py OUTPUT ENVIRONMENT_JSON COMMANDS_JSON
Commands are argv lists. Records actual mappings, library hashes, outputs and
kernel messages. Never changes installed desktop libraries or watchdog policy.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

out, config, commands = map(Path, sys.argv[1:])
parent = Path('/proc', str(os.getppid()), 'cmdline').read_bytes()
assert b'guard.py' in parent or b'guarded-command.py' in parent
assert (out / ('result.json' if b'guard.py' in parent else 'start.json')).exists()
env = dict(os.environ)
for key in list(env):
    if key.startswith(('ASAHI_', 'AGX_', 'HK_', 'MESA_', 'VK_', 'LIBGL_',
                       'GALLIUM_', '__EGL_', 'LD_')):
        del env[key]
env.update(json.loads(config.read_text()))
env.update(MESA_SHADER_CACHE_DISABLE='true', ASAHI_TEST_REPEAT='32', LD_DEBUG='libs')
assert 'MESA_GL_VERSION_OVERRIDE' not in env
assert 'MESA_GLES_VERSION_OVERRIDE' not in env

def save(name, value):
    (out / name).write_text(json.dumps(value, indent=2) + '\n')

save('environment.json', env)
prefix = Path(env['LD_LIBRARY_PATH'])
save('library-hashes.json', {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
     for p in prefix.glob('*.so*') if p.is_file()})
boot = Path('/proc/sys/kernel/random/boot_id').read_text()
results = []
with (out / 'probe-kernel.log').open('w') as kernel_log:
    kernel = subprocess.Popen(['dmesg', '--follow-new', '--color=never'],
                              stdout=kernel_log, stderr=kernel_log)
    try:
        for i, argv in enumerate(json.loads(commands.read_text())):
            argv = [s.replace('{output}', str(out)) for s in argv]
            mappings = set()
            start = time.monotonic()
            with (out / f'probe-{i}.log').open('w') as log:
                process = subprocess.Popen(argv, env=env, stdout=log,
                                           stderr=subprocess.STDOUT)
                while process.poll() is None:
                    try:
                        mappings.update(Path('/proc', str(process.pid), 'maps')
                                        .read_text().splitlines())
                    except (FileNotFoundError, ProcessLookupError):
                        pass
                    time.sleep(.02)
            (out / f'probe-{i}.maps').write_text('\n'.join(sorted(mappings))+'\n')
            results.append(dict(argv=argv, returncode=process.returncode,
                                seconds=time.monotonic()-start))
            save('probe-results.json', results)
            print((out / f'probe-{i}.log').read_text(), flush=True)
            assert process.returncode == 0, results[-1]
            # Short enumeration processes can finish between /proc polls.
            # The loader's actual initializer trace independently proves use.
            loaded = str(prefix / 'libvulkan_asahi.so')
            loader_log = (out / f'probe-{i}.log').read_text()
            assert (any(loaded in m for m in mappings) or
                    re.search(r'calling init:\s*' + re.escape(loaded), loader_log)), \
                'Candidate Vulkan library was not observed in process mappings'
        assert Path('/proc/sys/kernel/random/boot_id').read_text() == boot
        time.sleep(.2)
    finally:
        kernel.terminate()
        kernel.wait(timeout=2)
assert not re.search(r'BUG:|Oops:|Kernel panic|SError|firmware crashed|GPU fault|'
                     r'userspace GPU timeout|GPU retirement unproven|execution failed',
                     (out/'probe-kernel.log').read_text(), re.I)
print('PASS: merged Mesa probes completed with candidate mappings', flush=True)
