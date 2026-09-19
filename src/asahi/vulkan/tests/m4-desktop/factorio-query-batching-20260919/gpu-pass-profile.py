#!/usr/bin/env python3
"""Join self-contained M4 pass retirements to opt-in Mesa shader samples.

Run on retained bounded experiment logs. A CS address can be reused and a pass
can use many programs. Therefore report the whole observed shader set, never
divide its GPU cost between shaders or claim per-shader execution times.
The shader log must contain one process; --asid selects its GPU address space.
Instrumentation perturbs timing. GPU spans are not utilization counters.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re


def summarize(kernel, shaders, asid=None, tail_seconds=None):
    programs = defaultdict(set)
    current = {}
    submissions = defaultdict(list)
    metadata = {}
    for line in shaders.splitlines():
        boundary = re.match(r'HK_SHADER_(BEGIN|SUBMIT) cs=([\da-f]+)', line)
        if boundary:
            cs = int(boundary[2], 16)
            if boundary[1] == 'BEGIN':
                current[cs] = set()
            else:
                submissions[cs].append(tuple(sorted(current[cs])) if cs in current else None)
            continue
        if not line.startswith('HK_SHADER_PROFILE '):
            continue
        values = dict(re.findall(r'(\w+)=([\da-f]+)', line))
        if not values.get('cs') or int(values['cs'], 16) == 0:
            continue
        programs[int(values['cs'], 16)].add(values['code'])
        if int(values['cs'], 16) in current:
            current[int(values['cs'], 16)].add(values['code'])
        metadata[values['code']] = {k:int(v) for k,v in values.items()
                                    if k not in ('code', 'cs')}
    records = []
    for line in kernel.splitlines():
        if 'G16G_PASS ' not in line:
            continue
        values = dict(re.findall(r'(\w+)=(-?(?:0x[\da-f]+|\d+))', line))
        record = {k:int(v, 16 if v.startswith('0x') else 10)
                  for k,v in values.items()}
        timestamp = re.match(r'\[\s*([\d.]+)\]', line)
        record['log_time'] = float(timestamp[1]) if timestamp else None
        records.append(record)
    # Match complete per-address submission sequences before time filtering.
    # Buffers cannot be overwritten before their previous execution completes;
    # missing records or unknown recording starts make the match ineligible.
    retired = defaultdict(list)
    for r in records:
        if r['asid'] == asid:
            retired[r['cs']].append(r)
    exact = 0
    for cs, rows in retired.items():
        values = submissions.get(cs, [])
        if len(values) != len(rows) or any(v is None for v in values):
            continue
        for r, hashes in zip(rows, values):
            r['shaders'] = hashes
            exact += 1
    if tail_seconds is not None and records:
        times = [r['log_time'] for r in records if r['log_time'] is not None]
        if not times:
            raise ValueError('Tail selection needs timestamped kernel records')
        cutoff = max(times) - tail_seconds
        records = [r for r in records if r['log_time'] is not None and r['log_time'] >= cutoff]
    groups = {}
    for r in records:
        if asid is not None and r['asid'] != asid:
            continue
        # A numerical CS address from another address space is not a match.
        hashes = r.get('shaders', tuple(sorted(programs.get(r['cs'], ())))) if asid is not None else ()
        attribution = 'recorded submission' if 'shaders' in r else 'address lifetime union'
        key = (r['asid'], r['width'], r['height'], hashes, attribution)
        g = groups.setdefault(key, dict(asid=r['asid'], width=r['width'], height=r['height'],
            shaders=list(hashes), passes=0, ta_ns=0, gap_ns=0, fs_ns=0,
            max_fs_ns=0, control_streams=set(), attribution=attribution))
        g['passes'] += 1
        g['control_streams'].add(r['cs'])
        for field in ('ta_ns', 'gap_ns', 'fs_ns'):
            g[field] += r[field]
        g['max_fs_ns'] = max(g['max_fs_ns'], r['fs_ns'])
    for g in groups.values():
        g['control_streams'] = len(g['control_streams'])
        g['mean_fs_ms'] = g['fs_ns'] / g['passes'] / 1e6
    return dict(scope='Whole-pass timings grouped by observed shader sets; unpaired CS lifetimes are unions',
                exact_matches_before_tail=exact,
                address_spaces=sorted({r['asid'] for r in records}),
                groups=sorted(groups.values(), key=lambda g:g['fs_ns'], reverse=True),
                shaders=metadata)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('kernel', type=Path)
    p.add_argument('shaders', type=Path)
    p.add_argument('--asid', type=int)
    p.add_argument('--tail-seconds', type=float)
    a = p.parse_args()
    result = summarize(a.kernel.read_text(errors='replace'),
                       a.shaders.read_text(errors='replace'), a.asid, a.tail_seconds)
    print(json.dumps(result, indent=2))
