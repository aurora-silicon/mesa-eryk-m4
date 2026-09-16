#!/usr/bin/env python3
"""Target-only bounded Wayland command. MUST run inside gpu-experiment.py.

Uses the qualified Hyprland headless recipe from cts-wsi-headless-probe.py.
The compositor and child process groups are terminated even on timeout.
"""
import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--seconds", type=int, default=20, choices=range(1, 1201))
p.add_argument("--output", type=Path, required=True)
p.add_argument("command", nargs=argparse.REMAINDER)
a = p.parse_args()
if a.command[:1] == ["--"]:
    a.command.pop(0)
if not a.command:
    p.error("command required")
if not os.environ.get("M4_TEST_CPU_MAX_KHZ"):
    p.error("missing guard environment; invoke through gpu-experiment.py")
a.output.mkdir(parents=True, exist_ok=True)
greeter = None
for proc in Path("/proc").glob("[0-9]*"):
    try:
        cmd = (proc / "cmdline").read_bytes()
        if b"/opt/m4-hyprland/bin/Hyprland" in cmd and b"--watchdog-fd" in cmd:
            greeter = proc
            break
    except (FileNotFoundError, PermissionError):
        pass
if greeter is None:
    raise SystemExit("No qualified greeter environment found")
env = dict(entry.split("=", 1) for entry in (greeter / "environ").read_text().split("\0") if "=" in entry)
for key in list(env):
    if key.startswith(("HYPRLAND_", "XDG_", "DBUS_", "INVOCATION", "JOURNAL", "LISTEN", "NOTIFY", "AQ_DRM")) or key in ("WAYLAND_DISPLAY", "DISPLAY", "HOME", "USER", "LOGNAME", "PWD", "SHLVL", "_"):
        env.pop(key)
runtime = Path(tempfile.mkdtemp(prefix="m4-headless-"))
env.update(XDG_RUNTIME_DIR=str(runtime), HOME="/root", USER="root",
           AQ_HEADLESS_RENDER_NODE="/dev/dri/renderD128", AQ_USE_RENDER_NODE="1",
           HYPRLAND_NO_SD_NOTIFY="1")
config = a.output.resolve() / "headless.lua"
config.write_text('''hl.monitor({ output = "HEADLESS-1", mode = "1024x1024@1000", position = "0x0", scale = 1 })
hl.config({
 misc = { disable_hyprland_logo = true, disable_splash_rendering = true, force_default_wallpaper = 0 },
 general = { gaps_in = 0, gaps_out = 0, border_size = 0 },
 decoration = { rounding = 0, shadow = { enabled = false }, blur = { enabled = false } },
 animations = { enabled = false },
 debug = { disable_logs = false, enable_stdout_logs = true },
})
''')

def stop(proc):
    if proc is None:
        return
    try:
        os.killpg(proc.pid, signal.SIGTERM)
        proc.wait(timeout=3)
    except ProcessLookupError:
        pass
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()

compositor = child = None
def interrupted(signum, frame):
    raise SystemExit(128 + signum)

signal.signal(signal.SIGTERM, interrupted)
signal.signal(signal.SIGINT, interrupted)
try:
    with (a.output / "hyprland.log").open("wb") as log:
        compositor = subprocess.Popen(["/opt/m4-hyprland/bin/Hyprland", "--i-am-really-stupid", "--config", str(config)], env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + 10
        socket = None
        while time.monotonic() < deadline and compositor.poll() is None:
            socket = next((s for s in runtime.glob("wayland-*") if s.is_socket()), None)
            if socket:
                break
            time.sleep(0.1)
        if socket is None:
            raise RuntimeError("Headless compositor failed to create a Wayland socket")
        # Keep the command's explicit driver configuration; only replace display.
        child_env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY=socket.name)
        child_env.pop("DISPLAY", None)
        print("Headless Wayland:", runtime, socket.name, "1024x1024@1000", flush=True)
        child = subprocess.Popen(a.command, env=child_env, start_new_session=True)
        result = child.wait(timeout=a.seconds)
        text = (a.output / "hyprland.log").read_text(errors="replace")
        if "using available mode 1024x1024@1000.00Hz" not in text:
            raise RuntimeError("Headless mode was not confirmed by compositor log")
        raise SystemExit(result)
finally:
    stop(child)
    stop(compositor)
    shutil.rmtree(runtime)
