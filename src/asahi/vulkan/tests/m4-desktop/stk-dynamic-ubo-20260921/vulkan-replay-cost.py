#!/usr/bin/env python3
"""Summarize complete profiled replay submissions and compare the retained image.

No hardware execution. Requires HK_ARCHITECTURE_PROFILE/HK_M4_SHADER_PROFILE
and kernel SubmitTiming. Counts are selected explicitly, not inferred as FPS.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import statistics
import numpy as np
from PIL import Image

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('run', type=Path)
p.add_argument('--render-passes', type=int, required=True)
p.add_argument('--frame', type=int, default=3)
p.add_argument('--reference', type=Path, required=True)
a = p.parse_args()
spec = importlib.util.spec_from_file_location('cost', Path(__file__).with_name('gpu-frame-cost.py'))
cost = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cost)
kernel = (a.run/'remote/kernel-live.log').read_text()
mesa = (a.run/'remote/command.log').read_text()
candidates = []
for asid in sorted(set(map(int, re.findall(r'G16G_PASS .*?asid=(\d+)', kernel)))):
    report = cost.summarize(kernel, mesa, asid)
    frames = [s for s in report['submissions'] if s['render_count'] == a.render_passes and s['complete_command_timestamps']]
    if frames:
        candidates.append((report, frames))
assert len(candidates) == 1, 'Need one unambiguous replay address space'
report, frames = candidates[0]
assert not report['unmatched'], 'Incomplete retirement attribution'
actual_path = a.run/f'remote/vulkan-replay/screenshot_frame_{a.frame}.png'
actual = np.asarray(Image.open(actual_path).convert('RGBA'), dtype=np.int16)
reference = np.asarray(Image.open(a.reference).convert('RGBA'), dtype=np.int16)
assert actual.shape == reference.shape
error = np.abs(actual-reference)
summary = dict(asid=report['asid'], submissions=len(frames),
    scope='Instrumented complete GPU submission spans; not live FPS',
    median_gpu_ms=statistics.median(s['whole_submission_gpu_ms'] for s in frames),
    gpu_ms=[s['whole_submission_gpu_ms'] for s in frames],
    pass_fragment_ms=[[r['fs_ns']/1e6 for r in s['passes']] for s in frames],
    reference=str(a.reference), actual=str(actual_path),
    pixel_exact=bool(not error.any()), maximum_channel_error=int(error.max()),
    mean_channel_error=float(error.mean()), changed_pixels=int(np.count_nonzero(error.max(2))),
    pixels_over_8=int(np.count_nonzero(error.max(2)>8)))
(a.run/'frame-cost.json').write_text(json.dumps(report,indent=2)+'\n')
(a.run/'replay-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
