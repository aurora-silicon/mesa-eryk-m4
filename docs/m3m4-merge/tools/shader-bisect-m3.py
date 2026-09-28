#!/usr/bin/env python3
"""One guarded M3 shader-bisection step, using exact retained RGB references.

Requires an isolated diagnostic Mesa with nir_shader_bisect_select() around
the suspected transform. No score is imported as a benchmark qualification.
State records actual selected hashes, immutable run paths and pixel outcomes.
Explicit diagnostic tolerances must come from an existing reference policy;
they classify bisection outcomes only and never qualify benchmark scores.
Run repeatedly with the same state path; never in parallel on the M3.
"""
import argparse, json, re, shlex, subprocess
from pathlib import Path
import numpy as np
from PIL import Image

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('state', type=Path)
p.add_argument('--env', action='append', default=[])
p.add_argument('--case', default='satisfactory-vulkan-gameplay')
p.add_argument('--frame',type=int,default=0,help='Single-frame diagnostic instead of the full capture')
p.add_argument('--compiler-debug',help='Explicit compiler flags for the failing configuration')
p.add_argument('--machine', choices=['m3','m4'], default='m3')
p.add_argument('--channel-tolerance', type=int, default=0)
p.add_argument('--max-changed', type=int, default=0)
p.add_argument('--timeout', type=int, default=35, choices=range(1,171))
p.add_argument('--prefix', required=True)
p.add_argument('--sha256', required=True)
p.add_argument('--reference', required=True, type=Path)
a = p.parse_args()
bench = Path('/home/council/dev/gpu-replay-bench')
state = json.loads(a.state.read_text()) if a.state.exists() else {
    'compiler_debug': a.compiler_debug, 'prefix': a.prefix, 'sha256': a.sha256, 'reference': str(a.reference), 'env': a.env, 'machine': a.machine, 'case': a.case, 'frame': a.frame, 'channel_tolerance': a.channel_tolerance, 'max_changed': a.max_changed, 'steps': []}
assert state.get('compiler_debug') == a.compiler_debug
assert state.get('env', []) == a.env
assert state.get('case', 'satisfactory-vulkan-gameplay') == a.case
assert state.get('machine', 'm3') == a.machine
assert state.get('frame',0) == a.frame
assert state.get('channel_tolerance', 0) == a.channel_tolerance
assert state.get('max_changed', 0) == a.max_changed
assert (state['prefix'], state['sha256'], state['reference']) == (a.prefix, a.sha256, str(a.reference))
bad = state.get('remaining')
if bad is not None and len(bad) <= 1:
    print(json.dumps(state, indent=2)); raise SystemExit(0)
lo, hi = (bad[0], bad[(len(bad)-1)//2]) if bad else ('0'*64, 'f'*64)
step = a.state.with_name(a.state.stem + f'-{len(state["steps"]):02d}.log')
cmd = ['python3', str(bench/'tools'/('replay-m3.py' if a.machine == 'm3' else 'run-m4.py')), a.case,
       '--mesa-prefix', a.prefix, '--expected-mesa-sha256', a.sha256,
       '--mode', 'check', '--timeout', str(a.timeout), '--import-results',
       '--diagnostic-replayer', '/root/gpu-replay-bench/bin/gfxrecon-replay']
if a.machine == 'm4':
    for option in ('--expected-mesa-sha256', '--diagnostic-replayer'):
        index = cmd.index(option)
        del cmd[index:index+2]
    observed = subprocess.check_output(['ssh', 'root@omarchy-m4',
        shlex.join(['sha256sum', a.prefix + '/lib/libvulkan_asahi.so'])],
        text=True, timeout=30).split()[0]
    assert observed == a.sha256
if a.compiler_debug is not None:
    cmd += ['--compiler-debug', a.compiler_debug]
if a.frame:
    cmd += ['--diagnostic-frame',str(a.frame)]
for key, value in [('MESA_SHADER_CACHE_DISABLE', 'true'), ('NIR_SHADER_BISECT_LO', lo),
                   ('NIR_SHADER_BISECT_HI', hi)]:
    cmd += ['--diagnostic-env', key + '=' + value]
for item in a.env:
    cmd += ['--diagnostic-env', item]
print(f'Running range {lo}..{hi}', flush=True)
with step.open('x') as log:
    subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=420)
run = Path(re.search(r'Evidence: (.+)', step.read_text())[1])
remote = run/'remote'
result = json.loads((remote/'replay-result.json').read_text())
assert (result.get('diagnostic_completed') or result.get('success')) and result['returncode'] == 0
record_path = bench/'results'/a.case/a.machine/run.name/'benchmark.json'
if record_path.exists():
    record = json.loads(record_path.read_text())
    record['diagnostic_only'] = True
    record['diagnostic_reason'] = 'Shader transform bisection; never benchmark qualification'
    record_path.write_text(json.dumps(record, indent=2)+'\n')
selected = sorted(set(re.findall(r'NIR bisect selected source_blake3: .*?\(([0-9a-f]{64})\)', (remote/'replay.log').read_text())))
assert selected, 'No selected shaders; no state update'
rows = []
for frame in ((a.frame,) if a.frame else (1,16,31)):
    name = f'screenshot_frame_{frame}.png'
    ref = np.array(Image.open(a.reference/name).convert('RGB'))
    got = np.array(Image.open(remote/name).convert('RGB'))
    assert ref.shape == got.shape
    diff = abs(ref.astype(np.int16)-got.astype(np.int16))
    rows.append(dict(frame=frame, maximum=int(diff.max()), changed_pixels=int(np.any(diff, axis=2).sum())))
exact = all(row['maximum'] <= a.channel_tolerance and row['changed_pixels'] <= a.max_changed for row in rows)
assert bad is not None or not exact, 'Full selection does not reproduce discrepancy'
remaining = sorted(set(bad)-set(selected)) if exact else selected
assert remaining, 'No candidate remains; investigate interacting transforms'
record = dict(machine=a.machine, lo=lo, hi=hi, command=cmd, run=str(run), selected=selected, exact=exact, comparison=rows)
state['steps'].append(record); state['remaining'] = remaining
a.state.write_text(json.dumps(state, indent=2)+'\n')
(remote/'exact-comparison.json').write_text(json.dumps(rows, indent=2)+'\n')
print(json.dumps(dict(run=str(run), selected=len(selected), exact=exact, comparison=rows,
                     remaining=len(remaining), candidates=remaining if len(remaining)<5 else [])), flush=True)
