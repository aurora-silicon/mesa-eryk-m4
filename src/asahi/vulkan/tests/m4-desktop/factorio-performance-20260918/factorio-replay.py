#!/usr/bin/env python3
"""Replay the retained Factorio menu under gpu-experiment, without UI input.

Stops only our standalone benchmark service so its workload cannot contaminate
the replay. Optional shader capture excludes small meta shaders from the quota.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
p.add_argument('--icd', required=True)
p.add_argument('--compression', action='store_true', help='Use candidate compression allocation; not the unmodified captured allocation policy')
p.add_argument('--capture-shaders', action='store_true')
p.add_argument('--loop-count', type=int, choices=range(1,1001))
a = p.parse_args()
guard = json.loads((a.output / 'start.json').read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds'] == 60
# The replay runner must stop and retain its evidence before the outer guard
# deadline. A fixed 75-second inner timeout can outlive a 60-second guard.
assert guard['seconds'] >= 20
replay_seconds = min(75, guard['seconds'] - 12)
state = subprocess.check_output(['systemctl', 'show', '--property=LoadState',
    '--value', 'm4-factorio-standalone.service'], text=True, timeout=5).strip()
if state != 'not-found':
    subprocess.run(['systemctl', 'stop', 'm4-factorio-standalone.service'],
                   check=True, timeout=15)
env = os.environ.copy()
if a.capture_shaders:
    capture = a.output / 'frontend-nir'
    capture.mkdir()
    env.update(AGX_COMPILER_CAPTURE_SUCCESS='fragment',
               AGX_COMPILER_CAPTURE_MIN_INSTRUCTIONS='500',
               MESA_SHADER_CACHE_DISABLE='true',
               HK_NIR_CAPTURE_DIR=str(capture), HK_G17P_LAUNCH_TRACE='1')
command = ['python3', '/root/vulkan-trace.py', 'replay',
    '--guard-output', str(a.output), '--renderer', 'asahi', '--icd', a.icd,
    '--seconds', str(replay_seconds), '--trace',
    '/home/e/.factorio/menu_trim_trigger_20260918T042402.gfxr',
    '--memory-translation', 'rebind', '--omit-pipeline-cache',
    '--end-frame', '2']
if a.compression:
    command += ['--asahi-debug', '']
if a.loop_count:
    command += ['--loop-frame', '2', '--loop-count', str(a.loop_count)]
else:
    command += ['--frames', '2']
subprocess.run(command, env=env, check=True, timeout=replay_seconds + 8)
