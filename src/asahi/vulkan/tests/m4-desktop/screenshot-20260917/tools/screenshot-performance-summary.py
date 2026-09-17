#!/usr/bin/env python3
"""Qualify a fetched screenshot probe using completion cadence and saved PNG.

This is an offline evidence check, not a hardware workload. External timestamps
are sampled on the host CPU; panel timestamps originate in the kernel.
"""

import argparse
import json
from pathlib import Path
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("directory", type=Path, help="Fetched experiment's remote directory")
p.add_argument("--output", choices=("panel", "external"), required=True)
a = p.parse_args()
r = json.loads((a.directory / "screenshot-performance.json").read_text())
rows = []
for phase in r["phases"]:
    cadence = phase["panel_cadence"].copy()
    if a.output == "external":
        samples = json.loads((a.directory / "external-sampled-completions.json").read_text())
        samples = [x for x in samples if phase["start_monotonic"] * 1e9 <= x[0] <= phase["end_monotonic"] * 1e9]
        intervals = [(b[0] - c[0]) / 1e6 for c, b in zip(samples, samples[1:])]
        cadence = dict(
            intervals=len(intervals),
            missed_refreshes=sum(x > 25 for x in intervals),
            max_interval_ms=max(intervals, default=0),
            mean_interval_ms=sum(intervals) / len(intervals) if intervals else 0,
            sequence_contiguous=all(b[1] == c[1] + 1 for c, b in zip(samples, samples[1:])),
        )
    png_valid = False
    if phase.get("saved_png"):
        png = a.directory / "captures" / Path(phase["saved_png"]).name
        header = png.read_bytes()[:24]
        png_valid = (
            len(header) == 24
            and header[:8] == b"\x89PNG\r\n\x1a\n"
            and list(struct.unpack(">II", header[16:24])) == phase["saved_png_size"]
        )
    row = dict(
        phase=phase["name"], output=a.output,
        seconds=phase["seconds"], fps=phase[a.output + "_fps"],
        moving_warmup_seconds=phase.get("warmup_seconds", 0),
        cadence=cadence, saved_png_valid=png_valid,
        timestamp_source="kernel" if a.output == "panel" else "1 ms userspace sampling",
    )
    row["pass"] = (
        cadence["intervals"] >= phase["seconds"] * 55
        and cadence["missed_refreshes"] == 0
        and cadence["sequence_contiguous"]
        and 58 < row["fps"] < 62
        and (phase["name"] != "wrapper" or png_valid)
    )
    rows.append(row)
result = dict(pass_all=bool(rows) and all(x["pass"] for x in rows), measurements=rows)
(a.directory / "cadence-summary.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
raise SystemExit(0 if result["pass_all"] else 1)
