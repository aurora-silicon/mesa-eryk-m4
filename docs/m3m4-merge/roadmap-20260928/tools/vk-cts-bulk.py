#!/usr/bin/env python3
"""Frozen Vulkan batch under the existing M3 retirement/thermal watchdog guard.

Uses the already qualified deqp backend. Userspace incomplete results return
cleanly to the guard, but MUST stop the host controller for evidence review.
Kernel faults still escape the wrapper and retain watchdog recovery.
"""
import argparse
from contextlib import nullcontext
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import cts_deqp_backend as backend
from cts_headless import headless

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
p.add_argument('--cases', type=Path, required=True)
p.add_argument('--environment', type=Path, required=True)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--cache', type=Path, required=True)
a = p.parse_args()
out = a.output
assert b'guard.py' in Path('/proc', str(os.getppid()), 'cmdline').read_bytes()
assert json.loads((out/'result.json').read_text())['phase'] == 'thermal-policy-ready'
raw = a.cases.read_bytes()
meta = json.loads(a.cases.with_suffix('.meta.json').read_text())
assert hashlib.sha256(raw).hexdigest() == meta['cases_sha256']
assert raw.decode().splitlines() == [r['case'] for r in meta['cases']]
env = {k:v for k,v in os.environ.items() if not k.startswith(
    ('VK_', 'MESA_', 'AGX_', 'HK_', 'ASAHI_', 'LD_', 'GALLIUM_', 'LIBGL_', '__EGL_'))}
env.update(json.loads(a.environment.read_text()))
for k in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY'):
    env.pop(k, None)
boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
start = float(Path('/proc/uptime').read_text().split()[0])
fatal = re.compile(r'BUG:|Oops:|Kernel panic|SError|firmware crashed|co-processor has crashed|'
                   r'firmware error event|execution failed|GPU fault|M3 completion events', re.I)
last_check = 0

def save(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n')

def check(force=False):
    global last_check
    assert Path('/proc/sys/kernel/random/boot_id').read_text().strip() == boot
    if force or time.monotonic()-last_check >= .5:
        assert not fatal.search((out/'kernel-live.log').read_text(errors='replace')), 'GPU fault'
        last_check = time.monotonic()
    if force:
        log = subprocess.check_output(['dmesg', '--color=never'], text=True)
        recent = '\n'.join(line for line in log.splitlines()
            if (m := re.match(r'\[\s*([0-9.]+)\]', line)) and float(m[1]) >= start)
        (out/'kernel-final.log').write_text(recent)
        assert not fatal.search(recent), 'GPU fault during teardown'

with (out/'kernel-live.log').open('w') as log:
    kernel = subprocess.Popen(['dmesg', '--follow-new', '--color=never'], stdout=log, stderr=log)
try:
    wsi = any('.wsi.' in r['case'] for r in meta['cases'])
    # Xlib CTS creates override-redirect windows at the same origin. Keep
    # multi-swapchain windows visible in this private compositor so occlusion
    # does not replace frame callbacks with Xwayland's slow fallback timer.
    if any('.wsi.xlib.' in r['case'] for r in meta['cases']):
        env.setdefault('M3_CTS_TILE_X11', '1')
    with headless(out, env) if wsi else nullcontext(env) as display:
        save(out/'environment.json', {k:v for k,v in display.items() if k.startswith(
            ('VK_', 'MESA_', 'AGX_', 'HK_', 'ASAHI_', 'LD_', 'GALLIUM_', 'LIBGL_',
             '__EGL_', 'XDG_', 'WAYLAND_', 'DISPLAY', 'M3_CTS_'))})
        lib = Path(json.loads(Path(env['VK_DRIVER_FILES']).read_text())['ICD']['library_path'])
        save(out/'binary-hashes.json', {str(path):hashlib.sha256(path.read_bytes()).hexdigest()
             for path in (a.binary, a.runner, lib, Path('/root/m3-gpu-tools/asahi.ko'))})
        args = argparse.Namespace(cases=a.cases, start=0, count=len(meta['cases']),
            binary=a.binary, deqp_runner=a.runner, jobs=1, tests_per_group=1 if wsi else 50,
            case_timeout=60, runner_seconds=100, memory_floor_mib=1536, renderer='m3',
            log_images=False, log_sources=False, legacy_shader_cache=False,
            shader_cache_dir=a.cache, cpu_affinity=None, deqp_arg=[])
        try:
            backend.run(args, check, boot, out, display, meta, save)
            save(out/'batch-result.json', dict(status='complete'))
        except RuntimeError as exc:
            check()
            if not (out/'results.json').exists():
                raise
            save(out/'batch-result.json', dict(status='review-required', error=str(exc)))
    check(force=True)
finally:
    kernel.terminate()
    kernel.wait(timeout=2)
