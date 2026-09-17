#!/usr/bin/env python3
"""Compare retained picker scanouts without launching hardware workloads."""

import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("directory", type=Path, help="Fetched pixel probe's remote directory")
a = p.parse_args()
report = {}
for size in ("large", "small"):
    actual = (a.directory / f"optimized-{size}.m4fb").read_bytes()[32:]
    expected = (a.directory / f"reference-{size}.m4fb").read_bytes()[32:]
    assert len(actual) == len(expected) and actual, "Different or empty scanout sizes"
    report[size] = dict(
        bytes=len(actual),
        mismatched_bytes=sum(x != y for x, y in zip(actual, expected)),
    )
    for name in ("optimized", "reference"):
        repaint = a.directory / f"{name}-{size}-repaint.m4fb"
        if repaint.exists():
            original = (a.directory / f"{name}-{size}.m4fb").read_bytes()[32:]
            expected = repaint.read_bytes()[32:]
            assert len(original) == len(expected) and original
            report[f"{size}:{name}:full-repaint"] = dict(
                bytes=len(original),
                mismatched_bytes=sum(x != y for x, y in zip(original, expected)),
            )
(a.directory / "pixel-comparison.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
raise SystemExit(int(any(row["mismatched_bytes"] for row in report.values())))
