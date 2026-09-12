#!/usr/bin/env python3
"""Trim a saved Vulkan trace on x86 lavapipe, retaining GFXR initial state.

Uses an isolated Xvfb presentation surface: lavapipe does not expose the
frame-boundary extension needed for offscreen recapture. No J713 execution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import select
import signal
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('trace', type=Path)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--first', type=int, required=True)
p.add_argument('--last', type=int, required=True)
p.add_argument('--seconds', type=int, default=150, choices=range(1, 601))
a = p.parse_args()
if platform.machine() != 'x86_64' or not 1 <= a.first <= a.last:
    p.error('Requires x86 host and a positive ordered frame range')
root = Path(__file__).resolve().parent.parent
out = a.output.resolve(); out.mkdir(parents=True, exist_ok=False)
exe = root/'build/gfxreconstruct-host/tools/replay/gfxrecon-replay'
env = dict(os.environ)
for k in list(env):
    if k.startswith(('VK_', 'GFXRECON_', 'ASAHI_', 'AGX_', 'HK_', 'MESA_')):
        del env[k]
env.update(VK_DRIVER_FILES=str(root/'build/mesa-m4-reference/src/gallium/targets/lavapipe/lvp_devenv_icd.x86_64.json'),
           LP_NUM_THREADS='4', GFXRECON_CAPTURE_FRAMES=f'{a.first}-{a.last}',
           GFXRECON_CAPTURE_FILE=str(out/'trim.gfxr'), GFXRECON_CAPTURE_FILE_TIMESTAMP='false',
           GFXRECON_QUIT_AFTER_CAPTURE_FRAMES='true')
def sha(path):
    with path.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
manifest = dict(trace=str(a.trace.resolve()), trace_sha256=sha(a.trace),
                replay_sha256=sha(exe), frames=[a.first, a.last], seconds=a.seconds,
                adaptations=['lavapipe software replay', 'memory rebind',
                             'virtual X11 swapchain for recapture', 'omit pipeline cache'])
(out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
readfd, writefd = os.pipe()
server = child = None
try:
    with (out/'xvfb.log').open('w') as log:
        server = subprocess.Popen(['Xvfb', '-displayfd', str(writefd), '-screen', '0',
            '2560x1664x24', '-nolisten', 'tcp'], pass_fds=(writefd,), stdout=log, stderr=log)
    os.close(writefd)
    if not select.select([readfd], [], [], 8)[0]: raise TimeoutError('Xvfb startup')
    env['DISPLAY'] = ':'+os.read(readfd, 100).decode().strip(); os.close(readfd)
    command = [str(exe), '--capture', '--wsi', 'xcb', '--swapchain', 'virtual',
        '--present-mode', 'immediate', '--memory-translation', 'rebind', '--omit-pipeline-cache-data',
        '--screenshots', str(a.last-1), '--screenshot-dir', str(out), '--screenshot-format', 'png',
        '--measurement-frame-range', f'1-{a.last+1}', '--quit-after-measurement-range', str(a.trace.resolve())]
    manifest['command'] = command
    with (out/'replay.log').open('w') as log:
        child = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        result = child.wait(timeout=a.seconds)
    if result: raise RuntimeError(f'Recapture exited {result}')
    traces = list(out.glob('*.gfxr'))
    if not traces: raise RuntimeError('No trimmed trace produced')
    manifest['trace_outputs'] = {f.name: sha(f) for f in traces}
finally:
    if child and child.poll() is None:
        os.killpg(child.pid, signal.SIGKILL); child.wait(timeout=4)
    if server and server.poll() is None:
        server.terminate(); server.wait(timeout=4)
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print('Trim saved. Validate software replay and pixels before hardware use.')
