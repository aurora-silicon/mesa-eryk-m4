#!/usr/bin/env python3
"""Check hidden layer updates and exposure on J713 under gpu-experiment.

Requires the bounded wayland-layer-probe.c helper installed as
/usr/local/bin/m4-layer-occlusion-probe. The first advertised output must be
the 2560x1664 laptop panel at scale 2; wrong placement fails the pixel check.
"""
import json
import os
from pathlib import Path
import pwd
import signal
import struct
import subprocess
import sys
import time

out = Path(sys.argv[1])
for proc in Path("/proc").glob("[0-9]*"):
    try:
        if (proc / "cmdline").read_bytes().split(b"\0")[0] == b"/opt/m4-hyprland/bin/Hyprland":
            env = dict(x.split("=", 1) for x in (proc / "environ").read_text().split("\0") if "=" in x)
            uid = proc.stat().st_uid
            break
    except (OSError, ValueError):
        continue
else:
    raise RuntimeError("No user desktop")
assert uid == 1000
account = pwd.getpwuid(uid)
env["WAYLAND_DISPLAY"] = "wayland-1"
scanout = next(Path("/sys/kernel/debug/dri").glob("*/m4_scanout"))
status = scanout.with_name("m4_status")
children = []
results = {}


def launch(command, **kwargs):
    child = subprocess.Popen(command, env=env, user=uid, group=account.pw_gid,
                             extra_groups=os.getgrouplist(account.pw_name, account.pw_gid),
                             start_new_session=True, **kwargs)
    children.append(child)
    return child


def completed():
    return int(dict(x.split() for x in status.read_text().splitlines() if len(x.split()) == 2)["completed"])


def pixel():
    raw = scanout.read_bytes()
    _, width, height, stride, *_ = struct.unpack("<8I", raw[:32])
    assert (width, height) == (2560, 1664)
    offset = 32 + 220 * stride + 220 * 4
    return raw[offset:offset + 3].hex()


def change(color):
    source.stdin.write(color + "\n")
    source.stdin.flush()
    assert source.stdout.readline().strip() == "committed"
    time.sleep(0.35)


try:
    source = launch(["/usr/local/bin/m4-layer-occlusion-probe"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    assert source.stdout.readline().strip() == "committed"
    time.sleep(0.7)
    results["initial_pixel"] = pixel()
    assert results["initial_pixel"] == "0000ff", results
    freeze = launch(["/usr/local/bin/hyprpicker", "-r", "-z"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    before = completed()
    for color in ("b", "r", "b"):
        change(color)
        assert pixel() == "0000ff", "Hidden changes escaped the frozen image"
    results["hidden_panel_completions"] = completed() - before
    assert results["hidden_panel_completions"] == 0, results
    freeze.terminate()
    freeze.wait(timeout=3)
    time.sleep(0.7)
    results["exposed_pixel"] = pixel()
    assert results["exposed_pixel"] == "ff0000", results
    before = completed()
    change("r")
    results["visible_panel_completions"] = completed() - before
    results["updated_pixel"] = pixel()
    assert results["visible_panel_completions"] > 0 and results["updated_pixel"] == "0000ff", results
finally:
    for child in reversed(children):
        if child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=1)
    (out / "occlusion-results.json").write_text(json.dumps(results, indent=2) + "\n")
print("PASS: hidden commits stay idle; exposure and visible updates show current pixels")
