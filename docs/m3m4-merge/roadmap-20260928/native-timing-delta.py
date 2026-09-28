#!/usr/bin/env python3
"""Compare retained native retirement snapshots without treating spans as utilization.

Usage: native-timing-delta.py BEFORE_JSON AFTER_JSON [--output REPORT_JSON]
Accepts replay.py's native-timing-before/after.json on either M3 or M4.
This interval includes initialization and other GPU clients. Command active
spans overlap in batches; engine spans must not be summed as elapsed GPU time.
Reads saved evidence only; never accesses hardware.
"""
import argparse
import json
from pathlib import Path


def snapshot(path):
    record = json.loads(path.read_text())
    snapshots = record['snapshots']
    if len(snapshots) != 1:
        raise ValueError('Expected exactly one native timing device')
    device, text = next(iter(snapshots.items()))
    lines = text.strip().splitlines()
    header = dict(word.split('=', 1) for word in lines[0].split())
    if (header.get('version'), header.get('unit'), header.get('healthy')) != ('1', 'ns', '1'):
        raise ValueError('Unsupported or unhealthy native timing snapshot')
    engines = {}
    for line in lines[1:]:
        name, *words = line.split()
        if name in engines or name not in ('render', 'compute'):
            raise ValueError('Unexpected or duplicate engine')
        engines[name] = {k: int(v) for k, v in (word.split('=', 1) for word in words)}
    if set(engines) != {'render', 'compute'}:
        raise ValueError('Incomplete engine snapshot')
    return record['monotonic_ns'], device, header, engines


def compare(before, after):
    t0, device0, h0, e0 = snapshot(before)
    t1, device1, h1, e1 = snapshot(after)
    if device0 != device1 or h0['generation_ns'] != h1['generation_ns'] or t1 <= t0:
        raise ValueError('Different devices/generations or non-increasing observation time')
    result = dict(before=str(before), after=str(after), device=device0,
                  generation_ns=h0['generation_ns'], elapsed_wall_seconds=(t1-t0)/1e9,
                  scope=__doc__, engines={})
    for name in e0:
        if e0[name].keys() != e1[name].keys():
            raise ValueError('Counter schema changed')
        delta = {key: e1[name][key]-value for key, value in e0[name].items()}
        if any(value < 0 for value in delta.values()):
            raise ValueError('Counter reset or invalid snapshot ordering')
        result['engines'][name] = dict(
            counters=delta,
            seconds={key.removesuffix('_ns'): value/1e9 for key, value in delta.items()
                     if key.endswith('_ns')})
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(compare(args.before, args.after), indent=2)+'\n'
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end='')
