#!/usr/bin/env python3
"""Sample the external completion counter around the screenshot probe.

Run on the M4 ONLY under gpu-experiment's watchdog/thermal guard. These are
host observations sampled every millisecond, not firmware timestamps. The
command's own duration is bounded to 20 seconds; cleanup remains with the
screenshot probe. No kernel instrumentation or mode changes are performed.
"""
from pathlib import Path
import json
import subprocess
import sys
import time
import os
import pwd

out = Path(sys.argv[1])
probe = Path(__file__).with_name("screenshot-performance-probe.py")
counter = Path("/sys/module/m4_dcpext_desktop/parameters/native_kms_frames")
# Fail before launching a client if the external driver is absent.
stream = counter.open()
arguments = sys.argv[1:]
hidden = None
hidden_log = None
if "--hidden-layer-updates" in arguments:
    arguments.remove("--hidden-layer-updates")
    for proc in Path("/proc").glob("[0-9]*"):
        try:
            if (proc / "cmdline").read_bytes().split(b"\0")[0] == b"/opt/m4-hyprland/bin/Hyprland":
                env = dict(x.split("=", 1) for x in (proc / "environ").read_text().split("\0") if "=" in x)
                uid = proc.stat().st_uid
                break
        except (OSError, ValueError):
            continue
    else:
        raise RuntimeError("No desktop for the hidden-layer test")
    assert uid == 1000
    account = pwd.getpwuid(uid)
    env["WAYLAND_DISPLAY"] = "wayland-1"
    hidden_log = (out / "hidden-layer.log").open("w")
    hidden = subprocess.Popen(
        ["/usr/local/bin/m4-layer-occlusion-probe"], env=env,
        user=uid, group=account.pw_gid,
        extra_groups=os.getgrouplist(account.pw_name, account.pw_gid),
        stdin=subprocess.PIPE, stdout=hidden_log, stderr=hidden_log, text=True,
    )
    time.sleep(0.4)
child = subprocess.Popen([sys.executable, str(probe), *arguments])
hidden_start = time.monotonic()
hidden_updates = 0
samples = []
last = None
governors = []
last_governor = 0
try:
    with stream:
        while child.poll() is None:
            stream.seek(0)
            value = int(stream.read())
            stamp = time.monotonic_ns()
            if value != last:
                samples.append((stamp, value))
                last = value
            if stamp - last_governor >= 500_000_000:
                last_governor = stamp
                governors.append((stamp, {
                    str(p): p.read_text().strip()
                    for p in Path("/sys/devices/system/cpu/cpufreq").glob("policy*/scaling_governor")
                }))
            if hidden and time.monotonic() - hidden_start > 2 + hidden_updates:
                assert hidden.poll() is None
                hidden.stdin.write("b\n" if hidden_updates % 2 == 0 else "r\n")
                hidden.stdin.flush()
                hidden_updates += 1
            time.sleep(0.001)
    if child.returncode:
        raise RuntimeError(f"Screenshot probe failed: {child.returncode}")
finally:
    if hidden:
        hidden.terminate()
        hidden.wait(timeout=2)
        hidden_log.close()
        (out / "hidden-layer-updates.json").write_text(json.dumps(dict(updates=hidden_updates)) + "\n")
    if child.poll() is None:
        child.terminate()
        child.wait(timeout=3)
    (out / "external-sampled-completions.json").write_text(json.dumps(samples) + "\n")
    (out / "picker-governors.json").write_text(json.dumps(governors) + "\n")
print("PASS: sampled external completions")
