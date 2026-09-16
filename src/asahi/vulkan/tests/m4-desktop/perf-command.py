#!/usr/bin/env python3
"""Profile a command inside an existing hardware guard, preserving its arguments.

Set M4_PERF_BINARY and M4_PERF_OUTPUT_DIR, then use this as cts-bulk's --binary.
Requires the target's private /opt/m4-profiling/usr/bin/perf. This wrapper does
not arm a watchdog or impose bounds; the mandatory outer guard must do both.
Each invocation retains a unique profile and a manifest, including discovery
invocations from deqp-runner. Fetch the output directory into run evidence.
"""
import json
import os
from pathlib import Path
import sys

directory = Path(os.environ["M4_PERF_OUTPUT_DIR"])
directory.mkdir(parents=True, exist_ok=True)
stem = directory / f"profile-{os.getpid()}"
command = ["/usr/bin/env", "-u", "LD_LIBRARY_PATH"]
if "LD_LIBRARY_PATH" in os.environ:
    command.append("LD_LIBRARY_PATH=" + os.environ["LD_LIBRARY_PATH"])
command += [os.environ["M4_PERF_BINARY"], *sys.argv[1:]]
perf = "/opt/m4-profiling/usr/bin/perf"
argv = [perf, "record", "-e", "cpu-clock:u", "-F", "99",
        "--call-graph", "dwarf,8192", "-o", str(stem) + ".data", "--", *command]
stem.with_suffix(".json").write_text(json.dumps({"command": argv}, indent=2) + "\n")
os.execve(perf, argv, {**os.environ,
                      "LD_LIBRARY_PATH": "/opt/m4-profiling/usr/lib",
                      "DEBUGINFOD_URLS": ""})
