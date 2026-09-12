#!/usr/bin/env python3
"""Target-side GL export runner. Invoke ONLY inside gpu-experiment.py's guard.

Usage: run-gl-trace-export.py EXPORT_DIRECTORY EVIDENCE_DIRECTORY
Stops the greeter without rendering its exit animation, runs the exported GL
stream through run-m4-gl.py, and saves readback only after a successful replay.
The outer guard remains responsible for kernel-fault and thermal validation.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

source, output = map(Path, sys.argv[1:])
def gpu_clients():
    result = subprocess.run(["fuser", "/dev/dri/renderD128"], capture_output=True)
    if result.returncode not in (0, 1):
        raise RuntimeError("Cannot enumerate GPU clients: " + repr(result.stderr))
    return {int(pid) for pid in result.stdout.split()}

def send(pid, sig):
    try:
        os.kill(pid, sig)
    except ProcessLookupError:
        pass

main = int(subprocess.check_output(["systemctl", "show", "sddm.service", "-p", "MainPID", "--value"]))
if main:
    # Prevent the display manager from respawning clients while stopping them.
    # SIGSTOP also covers GPU clients in separate login session cgroups.
    send(main, signal.SIGSTOP)
clients = gpu_clients()
for pid in clients:
    send(pid, signal.SIGSTOP)
if clients:
    # Allow already accepted GPU work to retire without process teardown.
    # This is not a retirement proof; the mandatory outer guard still rejects
    # any kernel fault, including one during this bounded quiescence interval.
    time.sleep(2.5)
    for pid in clients:
        send(pid, signal.SIGKILL)
subprocess.run(["systemctl", "--no-block", "stop", "sddm.service", "user@1000.service"], timeout=3)
if main:
    send(main, signal.SIGCONT)
deadline = time.monotonic() + 5
while gpu_clients():
    if time.monotonic() >= deadline:
        raise RuntimeError("GPU clients remain after bounded teardown")
    time.sleep(0.1)
facts = {
    "files": {name: hashlib.sha256((source / name).read_bytes()).hexdigest()
              for name in ("replay.c", "m4-replay", "manifest.json")},
    "diagnostics": {key: value for key, value in os.environ.items()
                    if key.startswith(("M4_GL_REPLAY_", "HK_", "ZINK_", "AGX_"))},
}
(output / "replay-identity.json").write_text(json.dumps(facts, indent=2) + "\n")
shutil.copy2(source / "manifest.json", output / "replay-manifest.json")
os.chdir(source)
result = subprocess.call([sys.executable, "/root/run-m4-gl.py", str(source / "m4-replay")])
if result == 0:
    shutil.copy2("output.rgba", output / "output.rgba")
sys.exit(result)
