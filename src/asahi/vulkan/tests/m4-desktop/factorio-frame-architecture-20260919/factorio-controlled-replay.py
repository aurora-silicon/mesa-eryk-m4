#!/usr/bin/env python3
"""Guarded current Factorio capture with exact pass timings and pixel comparison.

Requires kernel SubmitTiming + SubmitSummary (debug_flags=25165824). Uses only
the experiment's live kernel log: whole-boot logs can contain identical reused
control-stream addresses from previous clients. Does not install a driver.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--name', required=True)
p.add_argument('--hypothesis', required=True)
p.add_argument('--icd', default='/opt/asahi-m4-port/asahi_icd.json')
p.add_argument('--env', action='append', default=[])
p.add_argument('--reference', type=Path, default=ROOT / '_provenance/experiments/'
    '20260919T080918619848Z-factorio-mcc-cache-replay/remote/vulkan-replay/screenshot_frame_5.png')
a = p.parse_args()
env = dict.fromkeys(('HK_ARCHITECTURE_PROFILE', 'HK_M4_SHADER_PROFILE',
    'MESA_SHADER_CACHE_DISABLE', 'HK_M4_COMPUTE_TIMESTAMP_COPIES',
    'HK_M4_BATCH_QUERY_COPIES', 'HK_M4_SPECIALIZE_QUERY_COPY',
    'HK_M4_NATIVE_PREAMBLE', 'AGX_NATIVE_FLOAT_PREAMBLE',
    'HK_M4_PREFETCH_DESCRIPTORS', 'HK_M4_BATCH_DISJOINT_CLEARS',
    'HK_M4_COMPRESSED_2D', 'HK_M4_COMPRESSED_FEEDBACK',
    'HK_M4_COMPUTE_COMPRESSED_COPY', 'HK_M4_COMPRESSION_AUDIT'), 'true')
for entry in a.env:
    key, sep, value = entry.partition('=')
    if not sep or not re.fullmatch('[A-Z][A-Z0-9_]*', key):
        p.error('environment must be NAME=value')
    env[key] = value
before = set((ROOT / '_provenance/experiments').glob('*-' + a.name))
command = [sys.executable, str(ROOT / 'tools/gpu-experiment.py'),
    '--name', a.name, '--hypothesis', a.hypothesis,
    '--expect', 'PASS: Vulkan replay process completed', '--seconds', '65',
    '--cpu-max-khz', '4104000', '--', 'env',
    *[f'{k}={v}' for k,v in env.items()],
    'python3', '/root/factorio-replay.py', '{output}', '--compression',
    '--icd', a.icd, '--trace', '/home/e/.local/share/factorio-driver/'
    'frame-architecture-20260919/menu_frames_1500_through_1504.gfxr', '--frames', '5']
result = subprocess.run(command, cwd=ROOT)
if result.returncode:
    raise SystemExit(result.returncode)
created = set((ROOT / '_provenance/experiments').glob('*-' + a.name)) - before
assert len(created) == 1, created
directory = created.pop()
assert json.loads((directory / 'report.json').read_text())['verdict'] == 'pass'
kernel = (directory / 'remote/kernel-live.log').read_text()
mesa = (directory / 'remote/command.log').read_text()
spec = importlib.util.spec_from_file_location('cost', ROOT / 'tools/gpu-frame-cost.py')
cost = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cost)
candidates = []
for asid in sorted(set(map(int, re.findall(r'G16G_PASS .*?asid=(\d+)', kernel)))):
    report = cost.summarize(kernel, mesa, asid)
    frames = [row for row in report['submissions'] if row['render_count'] == 10]
    if len(frames) == 5 and not report['unmatched']:
        candidates.append(report)
assert len(candidates) == 1, 'Missing or ambiguous complete five-frame GPU evidence'
report = candidates[0]
reference = Image.open(a.reference).convert('RGBA')
actual = Image.open(directory / 'remote/vulkan-replay/screenshot_frame_5.png').convert('RGBA')
report.update(evidence=str(directory), kernel_log='remote/kernel-live.log',
    reference=str(a.reference), pixels_equal=reference.size == actual.size and
        reference.tobytes() == actual.tobytes())
(directory / 'frame-cost.json').write_text(json.dumps(report, indent=2) + '\n')
for row in report['submissions']:
    if row['render_count'] == 10:
        print('timeline', row['timeline'], 'fragment_ms', row['fragment_ms'],
            'render_span_ms', row['first_render_to_last_render_ms'])
assert report['pixels_equal'], 'Frame differs: retain evidence and inspect before accepting'
print('PASS: five matched frames and pixel-exact final image')
