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
p.add_argument('--capture-shaders', action='store_true')
a = p.parse_args()
guard = json.loads((a.output / 'start.json').read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds'] == 60
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
subprocess.run(['python3', '/root/vulkan-trace.py', 'replay',
    '--guard-output', str(a.output), '--renderer', 'asahi', '--icd', a.icd,
    '--seconds', '75', '--trace',
    '/home/e/.factorio/menu_trim_trigger_20260918T042402.gfxr',
    '--memory-translation', 'rebind', '--omit-pipeline-cache',
    '--end-frame', '2', '--frames', '2'], env=env, check=True, timeout=80)
