#!/usr/bin/env python3
"""Compare picker scanout against stock slurp; run inside gpu-experiment."""

import argparse, json, os, signal, subprocess, time, re, pwd
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("output", type=Path)
p.add_argument("--smoke-default", action="store_true")
p.add_argument("--repaint-check", action="store_true")
a = p.parse_args()
procs = []
for proc in Path("/proc").glob("[0-9]*"):
    try:
        if (proc / "cmdline").read_bytes().split(b"\0")[
            0
        ] == b"/opt/m4-hyprland/bin/Hyprland":
            env = dict(
                s.split("=", 1)
                for s in (proc / "environ").read_text().split("\0")
                if "=" in s
            )
            uid = proc.stat().st_uid
            break
    except (OSError, ValueError):
        pass
else:
    raise RuntimeError("No desktop")
account = pwd.getpwuid(uid)
env["WAYLAND_DISPLAY"] = "wayland-1"
env["HYPRLAND_INSTANCE_SIGNATURE"] = next(
    Path(env["XDG_RUNTIME_DIR"]).glob("hypr/*/.socket.sock")
).parent.name


def ctl(*args):
    return subprocess.check_output(["hyprctl", *args], env=env, text=True, timeout=3)


def launch(command, name):
    log = (a.output / (name + ".log")).open("w")
    child = subprocess.Popen(
        command, env=env, user=uid, group=account.pw_gid,
        extra_groups=os.getgrouplist(account.pw_name, account.pw_gid),
        stdout=log, stderr=log, start_new_session=True
    )
    procs.append((child, log))
    return child


def stop(child):
    if child.poll() is None:
        os.killpg(child.pid, signal.SIGTERM)
        try:
            child.wait(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait(timeout=1)


mouse = subprocess.Popen(
    ["/root/wayland-pointer-probe"], env=env, stdin=subprocess.PIPE, text=True
)


def send(command):
    mouse.stdin.write(command + "\n")
    mouse.stdin.flush()


def capture(name):
    time.sleep(0.25)
    path = next(Path("/sys/kernel/debug/dri").glob("*/m4_scanout"))
    (a.output / (name + ".m4fb")).write_bytes(path.read_bytes())
    if a.repaint_check:
        rounding = json.loads(ctl("-j", "getoption", "decoration:rounding"))["int"]
        ctl("eval", f"hl.config({{decoration={{rounding={rounding}}}}})")
        time.sleep(0.25)
        (a.output / (name + "-repaint.m4fb")).write_bytes(path.read_bytes())


saved = json.loads(ctl("-j", "getoption", "cursor:no_hardware_cursors"))["int"]
pos = json.loads(ctl("-j", "cursorpos"))
try:
    ctl("eval", "hl.config({cursor={no_hardware_cursors=0}})")
    hp = launch(["/usr/local/bin/hyprpicker", "-r", "-z"], "freeze")
    time.sleep(0.4)
    for name, binary in [
        ("optimized", "/usr/local/bin/slurp"),
        ("reference", "/usr/bin/slurp"),
    ]:
        ctl("eval", "hl.dispatch(hl.dsp.cursor.move({x=40,y=80}))")
        slurp = launch([binary], name)
        time.sleep(0.2)
        send("m 2 1")
        time.sleep(0.1)
        send("b 1")
        time.sleep(0.05)
        for i in range(60):
            send("m 15 8")
            time.sleep(0.01)
        capture(name + "-large")
        for i in range(60):
            send("m -13 -7")
            time.sleep(0.01)
        capture(name + "-small")
        send("b 0")
        slurp.wait(timeout=2)
        time.sleep(0.2)
    if a.smoke_default:
        stop(hp)
        time.sleep(0.3)
        picker = launch(["/usr/local/bin/hyprpicker"], "default-color-picker")
        time.sleep(0.5)
        send("m 2 1")
        time.sleep(0.1)
        send("b 1")
        assert picker.wait(timeout=3) == 0
        send("b 0")
        assert re.search(
            r"#[0-9A-Fa-f]{6}", (a.output / "default-color-picker.log").read_text()
        )
        print(
            "PASS: default color picker returns a color and exits on selection",
            flush=True,
        )
finally:
    mouse.stdin.close()
    mouse.wait(timeout=2)
    for child, log in reversed(procs):
        stop(child)
        log.close()
    ctl("eval", f"hl.config({{cursor={{no_hardware_cursors={saved}}}}})")
    ctl("eval", f"hl.dispatch(hl.dsp.cursor.move({{x={pos['x']},y={pos['y']}}}))")
print("PASS: picker reference scanouts captured and desktop restored")
