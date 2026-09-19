#!/usr/bin/env python3
"""Join opt-in HK_ARCHITECTURE_PROFILE/HK_M4_SHADER_PROFILE to G16G_PASS.

One replay process per shader log. Select its ASID explicitly. These are GPU
pass spans, not wall-clock frame times or utilization. Unmatched lifetimes
are reported and excluded, never guessed from neighboring log lines.
"""
import argparse
import collections
import json
from pathlib import Path
import re


def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line.strip()))


def summarize(kernel, mesa, asid):
    recorded = collections.defaultdict(list)
    pending = None
    returns = {}
    for line in mesa.splitlines():
        if line.startswith('HK_ARCH_CS '):
            row = fields(line)
            pending = row if row['engine'] == 'render' else None
        elif line.startswith('HK_SHADER_SUBMIT ') and pending is not None:
            recorded[int(fields(line)['cs'], 16)].append(pending)
            pending = None
        elif line.startswith('HK_ARCH_RETURN '):
            row = fields(line)
            returns[(row['queue'], row['timeline'])] = int(row['elapsed_ns'])
    retired = collections.defaultdict(list)
    for line in kernel.splitlines():
        if 'G16G_PASS ' not in line:
            continue
        row = {k:int(v, 16 if v.startswith('0x') else 10)
               for k,v in fields(line.split('G16G_PASS ', 1)[1]).items()}
        if row['asid'] == asid:
            retired[row['cs']].append(row)
    submissions = {}
    unmatched = []
    for cs, rows in recorded.items():
        matches = retired.get(cs, [])
        if len(rows) != len(matches):
            unmatched.append(dict(cs=hex(cs), recorded=len(rows), retired=len(matches)))
            continue
        for meta, gpu in zip(rows, matches):
            key = (meta['queue'], meta['timeline'])
            dest = submissions.setdefault(key, dict(queue=int(key[0]), timeline=int(key[1]),
                submit_cpu_ms=returns.get(key, 0)/1e6, passes=[]))
            dest['passes'].append(dict(cb=int(meta['cb']), draws=int(meta['draws']),
                empty_tiles=int(meta['empty_tiles']), sample_bytes=int(meta['sample_bytes']),
                **gpu))
    for sub in submissions.values():
        sub['passes'].sort(key=lambda r:r['gpu_start'])
        sub['render_count'] = len(sub['passes'])
        sub['ta_ms'] = sum(r['ta_ns'] for r in sub['passes'])/1e6
        sub['fragment_ms'] = sum(r['fs_ns'] for r in sub['passes'])/1e6
        sub['ta_to_fragment_ms'] = sum(r['gap_ns'] for r in sub['passes'])/1e6
        sub['first_render_to_last_render_ms'] = (
            max(r['gpu_end'] for r in sub['passes']) - min(r['gpu_start'] for r in sub['passes']))/24000
    return dict(asid=asid, scope='Exact render pass lifetimes in one Vulkan submission. Spans between render passes include compute, queue gaps and other clients; not pure GPU execution or live FPS.',
        unmatched=unmatched, submissions=sorted(submissions.values(), key=lambda r:r['timeline']))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('kernel', type=Path)
    p.add_argument('mesa', type=Path)
    p.add_argument('--asid', type=int, required=True)
    a = p.parse_args()
    print(json.dumps(summarize(a.kernel.read_text(), a.mesa.read_text(), a.asid), indent=2))
