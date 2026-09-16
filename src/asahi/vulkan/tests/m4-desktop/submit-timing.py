#!/usr/bin/env python3
"""Summarize one serial CTS case with HK_SUBMIT_TIMING and kernel evidence.

Usage: tools/submit-timing.py RUN_DIRECTORY [--output report.json]
RUN_DIRECTORY contains cts-bulk's summary.json. Run exactly one case/worker.
Kernel timing requires asahi debug_flags bit 23 (SubmitTiming). Explicit kernel
CLOCK_MONOTONIC values are used: kmsg timestamps are a different clock domain.
"""
import argparse
import bisect
import json
from pathlib import Path
import re
import statistics


def distribution(values):
    ordered = sorted(values)
    if not ordered:
        return {"count": 0}
    return {"count": len(ordered), "median_ms": statistics.median(ordered),
            "p90_ms": ordered[int((len(ordered) - 1) * .9)],
            "max_ms": ordered[-1], "total_ms": sum(ordered)}


def summarize(run):
    summary = json.loads((run / "summary.json").read_text())
    if summary["selected"] != 1 or summary["jobs"] != 1:
        raise ValueError("Use one case and one worker; concurrent jobs cannot be paired by order")
    evidence = Path(summary["evidence"])
    logs = list((evidence / "remote/cts/deqp-runner").glob("*.c0.r1.log"))
    if len(logs) != 1:
        raise ValueError("Expected exactly one first-attempt case log")
    user = logs[0].read_text()
    kernel = (evidence / "remote/kernel-live.log").read_text()
    submits = [tuple(map(int, x)) for x in re.findall(
        r"HK_SUBMIT_TIMING t0_ns=(\d+) ioctl_us=(\d+) gap_us=(\d+)", user)]
    waits = [tuple(map(int, x)) for x in re.findall(
        r"HK_FENCE_TIMING start_ns=(\d+) end_ns=(\d+) result=(-?\d+)", user)]
    retires = [tuple(map(int, x)) for x in re.findall(
        r"userspace GPU job (\d+) retired .*?monotonic_ns=(\d+)", kernel)]
    if not submits or len(submits) != len(waits) or any(w[2] for w in waits):
        raise ValueError("Expected one successful fence wait per submission")
    # A greeter can submit independently. Accept only one retirement inside
    # each submit/wait interval, and fail instead of silently shifting pairs.
    paired = []
    for (start, _, _), (wait_start, end, _) in zip(submits, waits):
        if not start <= wait_start <= end:
            raise ValueError("Wait/submit order mismatch")
        matches = [(job, t) for job, t in retires if start <= t <= end]
        if len(matches) != 1:
            raise ValueError("Ambiguous kernel retirement in submit/wait interval")
        paired.append(matches[0])
    stats = {
        "submit_ioctl": [s[1] / 1000 for s in submits],
        "submit_to_retire": [(r[1] - s[0]) / 1e6 for s, r in zip(submits, paired)],
        "fence_wait": [(w[1] - w[0]) / 1e6 for w in waits],
        "retire_to_wait_return": [(w[1] - r[1]) / 1e6 for w, r in zip(waits, paired)],
        "wait_return_to_next_submit": [(s[0] - w[1]) / 1e6 for s, w in zip(submits[1:], waits)],
    }
    fences = {int(job): (int(a), int(b)) for job, a, b in re.findall(
        r"G16G_TIMING fence job=(\d+) start_ns=(\d+) end_ns=(\d+)", kernel)}
    if fences:
        if any(job not in fences for job, _ in paired):
            raise ValueError("Incomplete fence timing evidence")
        stats["fence_signal"] = [(fences[j][1] - fences[j][0]) / 1e6 for j, _ in paired]
        stats["signal_to_wait_return"] = [(w[1] - fences[j][0]) / 1e6 for w, (j, _) in zip(waits, paired)]
    commands = re.findall(r"G16G_TIMING command job=(\d+) index=(\d+) pipe=(\d+) monotonic_ns=(\d+) prepare_ns=(\d+) active_ns=(\d+)", kernel)
    jobs = {j for j, _ in paired}
    events = sorted(int(t) for t in re.findall(r"G16G_TIMING event monotonic_ns=(\d+)", kernel))
    for job, index, pipe, t, prep, active in map(lambda x: map(int, x), commands):
        if job not in jobs:
            continue
        for label, value in (("prepare", prep), ("active", active)):
            stats.setdefault(f"command_{index}_pipe_{pipe}_{label}", []).append(value / 1e6)
        prior = bisect.bisect_right(events, t) - 1
        if prior >= 0:
            stats.setdefault("latest_event_to_command_retire", []).append((t - events[prior]) / 1e6)
    return {"run": str(run), "evidence": str(evidence), "guard_verdict": summary["guard_verdict"],
            "scope": "Single serial CTS case; wait-return gap includes both application and driver CPU work. Command active time includes firmware/host waits, not just GPU execution.",
            "timings": {name: distribution(values) for name, values in stats.items()}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.dumps(summarize(args.run), indent=2) + "\n"
    if args.output:
        args.output.write_text(report)
    print(report, end="")
