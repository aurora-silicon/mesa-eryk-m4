#!/usr/bin/env python3
"""Locate the first Vulkan error in an existing offline fragment probe.

Accepts retained host-shim fragment probes, or a Vulkan trace with --trace.
Trace replay records explicit rebinding and capability adaptations. This never
runs an ARM binary or a hardware driver.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('probe', type=Path, help='Existing probe directory, or new evidence directory with --trace')
p.add_argument('--trace', type=Path, help='Replay this capture under the host no-op DRM shim')
args = p.parse_args()
directory = args.probe.resolve()
replay_args = []
if args.trace:
    root = Path(__file__).resolve().parent.parent
    directory.mkdir(parents=True, exist_ok=False)
    trace = args.trace.resolve(strict=True)
    binary = root/'build/gfxreconstruct-host/tools/replay/gfxrecon-replay'
    env = dict(ASAHI_SHIM_CHIP='g16g', ASAHI_M4_EXPERIMENTAL='1',
               MESA_SHADER_CACHE_DISABLE='true',
               LD_PRELOAD=str(root/'build/mesa-m4-asahi-host/src/asahi/drm-shim/libasahi_noop_drm_shim.so'),
               VK_DRIVER_FILES=str(root/'build/mesa-m4-asahi-host/src/asahi/vulkan/asahi_devenv_icd.x86_64.json'))
    replay_args = ['--swapchain','offscreen','--memory-translation','rebind',
                   '--omit-pipeline-cache-data','--remove-unsupported',str(trace)]
    files = [trace,binary,Path(env['LD_PRELOAD']),Path(env['VK_DRIVER_FILES']),
             root/'build/mesa-m4-asahi-host/src/asahi/vulkan/libvulkan_asahi.so']
    (directory/'manifest.json').write_text(json.dumps(dict(
        host_environment=env,command=[str(binary),*replay_args],
        rendering_qualification=False,
        sha256={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}),indent=2)+'\n')
else:
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
r = subprocess.run(['gdb', '-q', '-batch', '-x', str(script), '--args', str(binary), *replay_args],
                   capture_output=True, text=True, timeout=30)
log = directory / 'first-error.log'
log.write_text(r.stdout + r.stderr)
print(r.stdout[-6000:])
print(log)
