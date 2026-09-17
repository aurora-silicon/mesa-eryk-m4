#!/usr/bin/env python3
"""Summarize retained G16 render timing and Chromium frame counters (no GPU I/O).

Usage: buoy-profile-summary.py EXPERIMENT_DIRECTORY [EXPERIMENT_DIRECTORY ...]
GPU timestamps use the qualified 24 MHz firmware timebase. Render passes are
not frames: keep their timings separate from the page's animation counters.
"""
import argparse
import json
from pathlib import Path
import re
import statistics


def summarize(directory):
    before = json.loads((directory / 'target-before.json').read_text())
    groups = {}
    active = None
    for line in (directory / 'remote/kernel-journal.log').read_text().splitlines():
        stamp = re.match(r'\[\s*([0-9.]+)\]', line)
        if not stamp or float(stamp[1]) < before['uptime_seconds']:
            continue
        render = re.search(r'owned render (\d+)x(\d+) ASID=(\d+)', line)
        if render:
            active = tuple(map(int, render.groups()))
        timing = re.search(
            r'render GPU timestamps=\[\[([0-9a-f]+), ([0-9a-f]+)\], '
            r'\[([0-9a-f]+), ([0-9a-f]+)\]\]', line)
        if timing and active:
            ta0, ta1, fr0, fr1 = (int(x, 16) for x in timing.groups())
            if fr1 >= fr0 and ta1 >= ta0:
                groups.setdefault(active, []).append((fr1 - fr0) / 24000)
            active = None
    render_groups = []
    for (width, height, asid), times in groups.items():
        ordered = sorted(times)
        render_groups.append(dict(width=width, height=height, asid=asid,
                                  passes=len(times), fragment_total_ms=sum(times),
                                  fragment_median_ms=statistics.median(times),
                                  fragment_p95_ms=ordered[int(len(times) * .95)],
                                  fragment_max_ms=max(times)))
    samples = {}
    for line in (directory / 'remote/chromium/window-progress.jsonl').read_text().splitlines():
        row = json.loads(line)
        state = row.get('state', {}).get('result', {}).get('value', {})
        probe = state.get('probe')
        if probe and probe.get('ready') and probe.get('playing') and state.get('visibility') == 'visible':
            samples.setdefault(row['window'], []).append((row['monotonic'], probe['frames']))
    windows = []
    for window, rows in samples.items():
        intervals = [dict(seconds=t1-t0, frames=f1-f0, fps=(f1-f0)/(t1-t0))
                     for (t0, f0), (t1, f1) in zip(rows, rows[1:]) if t1 > t0]
        windows.append(dict(window=window, intervals=intervals))
    return dict(experiment=directory.name, boot_id=before['boot_id'],
                timestamp_filter='kernel timestamps >= recorded target uptime',
                render_groups=sorted(render_groups, key=lambda g: -g['fragment_total_ms']),
                page_animation=windows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    args = parser.parse_args()
    print(json.dumps([summarize(p) for p in args.directories], indent=2))
