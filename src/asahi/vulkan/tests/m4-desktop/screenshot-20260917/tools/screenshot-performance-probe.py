#!/usr/bin/env python3
"""Compare screenshot components on the live desktop under gpu-experiment (40s).

Moves only the cursor, restores cursor policy/position, and terminates only
children started here. Counts physical panel completions, not inferred FPS.
"""

import argparse, json, os, signal, subprocess, time, struct, math, pwd, tempfile, shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("output", type=Path)
p.add_argument("--phase")
p.add_argument("--duration", type=float, default=4)
p.add_argument("--perf", action="store_true")
p.add_argument("--perf-frequency", type=int, default=99)
p.add_argument("--hyprpicker", default="hyprpicker")
p.add_argument("--slurp", default="slurp")
p.add_argument("--wayland-debug", action="store_true")
p.add_argument("--scheduling", choices=["true", "false"])
p.add_argument("--strace", action="store_true")
p.add_argument("--motion", choices=("circle", "triangle"), default="circle")
p.add_argument("--x", type=int, default=300)
p.add_argument("--y", type=int, default=250)
p.add_argument("--width", type=int, default=0)
p.add_argument("--height", type=int, default=0)
a = p.parse_args()
if not 0 < a.duration <= 20:
    p.error("duration must be in (0, 20]; use the 40-second experiment guard")
procs = []
report = {}
perf = None
mouse = None
tracer = None
for f in Path("/proc").glob("[0-9]*/cmdline"):
    try:
        if f.read_bytes().split(b"\0")[0] == b"/opt/m4-hyprland/bin/Hyprland":
            pid = int(f.parent.name)
            env = dict(
                s.split("=", 1)
                for s in (f.parent / "environ").read_text().split("\0")
                if "=" in s
            )
            uid = f.parent.stat().st_uid
            break
    except (OSError, ValueError):
        pass
else:
    raise RuntimeError("No desktop Hyprland")
account = pwd.getpwuid(uid)
assert uid == 1000, "Use the user desktop, not the login greeter"
wayland = [f for f in Path(env["XDG_RUNTIME_DIR"]).glob("wayland-*") if f.is_socket()]
assert len(wayland) == 1, wayland
env["WAYLAND_DISPLAY"] = wayland[0].name
for s in Path(env["XDG_RUNTIME_DIR"]).glob("hypr/*/.socket.sock"):
    env["HYPRLAND_INSTANCE_SIGNATURE"] = s.parent.name
    if (
        subprocess.run(
            ["hyprctl", "-j", "monitors"],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode
        == 0
    ):
        break


def ctl(*args):
    return subprocess.check_output(["hyprctl", *args], env=env, text=True, timeout=3)


def stats():
    paths = {f.resolve() for f in Path("/sys/kernel/debug/dri").glob("*/m4_status")}
    return {
        str(f): dict(
            line.split()
            for line in f.read_text().splitlines()
            if len(line.split()) == 2
        )
        for f in paths
    }


def external_frames():
    path = Path("/sys/module/m4_dcpext_desktop/parameters/native_kms_frames")
    return int(path.read_text()) if path.exists() else 0


def cpu():
    return Path("/proc", str(pid), "stat").read_text().rsplit(")", 1)[1].split()[11:13]


def stop():
    for c, f in procs:
        if c.poll() is None:
            os.killpg(c.pid, signal.SIGTERM)
    for c, f in procs:
        try:
            c.wait(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(c.pid, signal.SIGKILL)
            c.wait(timeout=1)
        f.close()
    procs.clear()


# Protocol input reaches the selection client, unlike an IPC cursor warp.
mouse = subprocess.Popen(
    ["/root/wayland-pointer-probe"], env=env, stdin=subprocess.PIPE, text=True
)


def event(kind, code, value):
    if kind == 1:
        mouse.stdin.write(f"b {value}\n")
        mouse.stdin.flush()


def motion(i):
    if a.motion == "triangle":
        direction = 1 if (i // 60) % 2 == 0 else -1
        dx, dy = direction * 2, direction
    else:
        # A diagonal reversal can cancel both input deltas within one refresh,
        # legitimately producing no surface damage on a VFR compositor. Circle
        # motion has no stationary reversal. A 64-pixel radius also guarantees
        # each integer input step moves: smaller radii round some steps to 0,0
        # and those legitimate idle refreshes are not dropped frames.
        angle, next_angle = i * math.tau / 240, (i + 1) * math.tau / 240
        dx = round(64 * math.cos(next_angle)) - round(64 * math.cos(angle))
        dy = round(64 * math.sin(next_angle)) - round(64 * math.sin(angle))
    mouse.stdin.write(f"m {dx} {dy}\n")
    mouse.stdin.flush()


(a.output / "hyprland.maps").write_text(Path("/proc", str(pid), "maps").read_text())
saved = json.loads(ctl("-j", "getoption", "cursor:no_hardware_cursors"))["int"]
pos = json.loads(ctl("-j", "cursorpos"))
report.update(
    pid=pid,
    cursor_policy=saved,
    monitors=json.loads(ctl("-j", "monitors")),
    graphics_environment={
        k: v
        for k, v in env.items()
        if k.startswith(("MESA", "GALLIUM", "VK_", "ZINK", "AGX", "AQ_"))
    },
)
captures = Path(tempfile.mkdtemp(prefix="m4-screenshot-", dir=env["XDG_RUNTIME_DIR"]))
os.chown(captures, uid, uid)
env["OMARCHY_SCREENSHOT_DIR"] = str(captures)
saved_scheduling = json.loads(ctl("-j", "getoption", "render:new_render_scheduling"))[
    "bool"
]
try:
    if a.scheduling:
        ctl("eval", f"hl.config({{render={{new_render_scheduling={a.scheduling}}}}})")
    for name, policy, commands in [
        ("baseline", saved, []),
        ("cursor-policy", 0, []),
        ("freeze", 0, [["hyprpicker", "-r", "-z"]]),
        ("selection", 0, [["slurp"]]),
        ("combined", 0, [["hyprpicker", "-r", "-z"], ["slurp"]]),
        ("wrapper", saved, [["omarchy-capture-screenshot", "smart", "save"]]),
    ]:
        if (a.phase and name != a.phase) or (not a.phase and name == "wrapper"):
            continue
        ctl("eval", f"hl.config({{cursor={{no_hardware_cursors={policy}}}}})")
        ctl("eval", f"hl.dispatch(hl.dsp.cursor.move({{x={a.x},y={a.y}}}))")
        for command in commands:
            log = (a.output / (name + "-" + command[0] + ".log")).open("w")
            if command[0] == "hyprpicker":
                command[0] = a.hyprpicker
            if command[0] == "slurp":
                command[0] = a.slurp
            c = subprocess.Popen(
                command,
                env={**env, **({"WAYLAND_DEBUG": "1"} if a.wayland_debug else {})},
                user=uid,
                group=account.pw_gid,
                extra_groups=os.getgrouplist(account.pw_name, account.pw_gid),
                stdin=subprocess.DEVNULL,
                stdout=log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            procs.append((c, log))
            time.sleep(0.8)
        assert all(c.poll() is None for c, _ in procs), (
            "Screenshot component exited before measurement"
        )
        if a.strace:
            tracer = subprocess.Popen(
                [
                    "strace",
                    "-f",
                    "-ttt",
                    "-T",
                    "-e",
                    "trace=ioctl,poll,ppoll,epoll_wait,epoll_pwait,futex,nanosleep,clock_nanosleep,recvmsg,sendmsg",
                    "-p",
                    str(pid),
                    "-o",
                    str(a.output / (name + ".strace")),
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        if a.perf:
            perf = subprocess.Popen(
                [
                    "/opt/m4-profiling/usr/bin/perf",
                    "record",
                    "-e",
                    "cpu-clock:u",
                    "-F",
                    str(a.perf_frequency),
                    "--call-graph",
                    "fp",
                    "--user-regs=x0,x1,x2,x3,x4,x5,x6,x7,lr,pc",
                    "-p",
                    ",".join(str(n) for n in [pid, *[c.pid for c, _ in procs]]),
                    "-o",
                    str(a.output / (name + ".perf.data")),
                    "--",
                    "sleep",
                    str(a.duration),
                ],
                env={
                    **os.environ,
                    "LD_LIBRARY_PATH": "/opt/m4-profiling/usr/lib",
                    "DEBUGINFOD_URLS": "",
                },
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        motion(0)
        time.sleep(0.1)
        if name in ("selection", "combined", "wrapper"):
            event(1, 272, 1)
            event(0, 0, 0)
            mouse.stdin.write(f"m {a.width} {a.height}\n")
            mouse.stdin.flush()
        # Warm the actual moving path, not a stationary selection. Report the
        # steady-state interval separately from launch/first-draw latency.
        warmup_start = time.monotonic()
        warmup_moves = 0
        while time.monotonic() - warmup_start < 0.5:
            motion(warmup_moves)
            warmup_moves += 1
            time.sleep(1 / 120)
        (a.output / (name + ".maps")).write_text(
            Path("/proc", str(pid), "maps").read_text()
        )
        cursor_before = json.loads(ctl("-j", "cursorpos"))
        history = {}
        last_history = 0
        before = stats()
        external_before = external_frames()
        ticks = cpu()
        start = time.monotonic()
        calls = 0
        while time.monotonic() - start < a.duration:
            motion(warmup_moves + calls)
            calls += 1
            if time.monotonic() - last_history > 0.5:
                last_history = time.monotonic()
                for line in (
                    Path(next(iter(before)))
                    .with_name("m4_present_history")
                    .read_text()
                    .splitlines()[1:]
                ):
                    values = list(map(int, line.split()))
                    history[values[0]] = values
            time.sleep(1 / 120)
        # Retain the final half-second too; acceptance uses every completion
        # strictly inside the measured interval, not a sampled subset.
        for line in (
            Path(next(iter(before)))
            .with_name("m4_present_history")
            .read_text()
            .splitlines()[1:]
        ):
            values = list(map(int, line.split()))
            history[values[0]] = values
        elapsed = time.monotonic() - start
        after = stats()
        assert all(c.poll() is None for c, _ in procs), (
            "Screenshot component exited during measurement"
        )
        row = dict(
            name=name,
            cursor_before=cursor_before,
            cursor_after=json.loads(ctl("-j", "cursorpos")),
            seconds=elapsed,
            moves=calls,
            warmup_moves=warmup_moves,
            warmup_seconds=0.5,
            motion=a.motion,
            before=before,
            after=after,
            cpu_ticks_before=ticks,
            cpu_ticks_after=cpu(),
            children=[{"pid": c.pid, "returncode": c.poll()} for c, _ in procs],
        )
        for key in before:
            row["panel_fps"] = (
                int(after[key]["completed"]) - int(before[key]["completed"])
            ) / elapsed
        row["external_fps"] = (external_frames() - external_before) / elapsed
        row["start_monotonic"] = start
        row["end_monotonic"] = start + elapsed
        measured = [
            v
            for _, v in sorted(history.items())
            if start * 1e9 <= v[2] <= (start + elapsed) * 1e9
        ]
        intervals = [(y[2] - x[2]) / 1e6 for x, y in zip(measured, measured[1:])]
        row["panel_cadence"] = {
            "intervals": len(intervals),
            "missed_refreshes": sum(v > 25 for v in intervals),
            "max_interval_ms": max(intervals, default=0),
            "mean_interval_ms": sum(intervals) / len(intervals) if intervals else 0,
            "sequence_contiguous": all(
                y[0] == x[0] + 1 for x, y in zip(measured, measured[1:])
            ),
        }
        (a.output / (name + ".present-history.json")).write_text(
            json.dumps(sorted(history.values()))
        )
        report.setdefault("phases", []).append(row)
        print(json.dumps(row), flush=True)
        if name in ("selection", "combined", "wrapper"):
            event(1, 272, 0)
            event(0, 0, 0)
        if name == "wrapper":
            assert procs[0][0].wait(timeout=5) == 0, "Capture wrapper failed"
            files = list(captures.glob("*.png"))
            assert len(files) == 1, files
            png = files[0].read_bytes()
            assert png[:8] == b"\x89PNG\r\n\x1a\n"
            row["saved_png"] = str(a.output / "captures" / files[0].name)
            row["saved_png_size"] = struct.unpack(">II", png[16:24])
        if perf:
            perf.wait(timeout=3)
            perf = None
        if tracer:
            tracer.terminate()
            tracer.wait(timeout=3)
            tracer = None
        stop()
        time.sleep(0.3)
finally:
    if tracer and tracer.poll() is None:
        tracer.terminate()
        tracer.wait(timeout=3)
    if a.scheduling:
        ctl(
            "eval",
            f"hl.config({{render={{new_render_scheduling={str(bool(saved_scheduling)).lower()}}}}})",
        )
    if mouse is not None:
        mouse.stdin.close()
        mouse.wait(timeout=3)
    if perf and perf.poll() is None:
        perf.terminate()
        perf.wait(timeout=3)
    stop()
    ctl("eval", f"hl.config({{cursor={{no_hardware_cursors={saved}}}}})")
    ctl("eval", f"hl.dispatch(hl.dsp.cursor.move({{x={pos['x']},y={pos['y']}}}))")
    shutil.copytree(captures, a.output / "captures", dirs_exist_ok=True)
    shutil.rmtree(captures)
    (a.output / "screenshot-performance.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )
print(
    "PASS: screenshot component measurements saved and desktop state restored",
    flush=True,
)
