#!/usr/bin/env python3
"""Bounded target-only Steam-runtime probe; invoke inside gpu-experiment.py.

Creates a private runtime directory for user e. Never joins an existing VM.
The guest path must contain the explicitly staged qualification binaries.
"""
import argparse
import os
from pathlib import Path
import pwd
import pty
import select
import shutil
import signal
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--mode", choices=("info", "native", "fex", "gl32", "gl64", "runtime64"), default="info")
p.add_argument("--seconds", type=int, default=60, choices=range(1, 121))
p.add_argument("--guest", action="store_true")
p.add_argument("--gdb", action="store_true")
p.add_argument("--trace", action="store_true")
p.add_argument("--validation", action="store_true")
p.add_argument("--overlay", type=Path)
p.add_argument("--host-drm-trace", action="store_true")
a = p.parse_args()
base = Path("/opt/m4-dx11-qualification")

if a.guest:
    print("Guest kernel:", os.uname(), flush=True)
    print("Guest page size:", os.sysconf("SC_PAGE_SIZE"), flush=True)
    if os.sysconf("SC_PAGE_SIZE") != 4096:
        raise SystemExit("FAIL: expected 4 KiB guest pages")
    if a.mode == "info":
        print("PASS: microVM boot with 4 KiB pages", flush=True)
        raise SystemExit(0)
    arch = "arm64" if a.mode == "native" else "x86_64"
    env = dict(os.environ, VK_DRIVER_FILES=str(base / arch / "icd.json"),
               LD_LIBRARY_PATH=str(base / arch), ASAHI_M4_EXPERIMENTAL="1",
               DXVK_WSI_DRIVER="SDL3", SDL_VIDEODRIVER="offscreen",
               AGX_MESA_DEBUG="", HK_G17P_LAUNCH_TRACE=str(int(a.trace)),
               HK_DEBUG_LOG='true',
               DXVK_DEBUG="validation" if a.validation else "",
               MESA_SHADER_CACHE_DISABLE="true", ASAHI_MESA_DEBUG="nocompress")
    if a.mode == "fex":
        env["VK_LOADER_DEBUG"] = "error,warn"
    if a.overlay:
        env.update(VK_DRIVER_FILES='/opt/m4-steam/icd.json',
                   LD_LIBRARY_PATH='/opt/asahi-m4-port/lib:/opt/asahi-m4-port/lib32:' + env['LD_LIBRARY_PATH'],
                   LIBGL_DRIVERS_PATH='/opt/asahi-m4-port/lib/dri:/opt/asahi-m4-port/lib32/dri',
                   __EGL_VENDOR_LIBRARY_FILENAMES='/opt/m4-steam/egl.json')
    exe = "d3d11-smoke-arm64-r2" if a.mode == "native" else "d3d11-smoke-host-present"
    command = [str(base / arch / exe), "m4"] + [str(base / "arm64" / (s + ".dxbc")) for s in ("vs", "ps", "cs")]
    if a.mode == "fex":
        command.insert(0, "/usr/bin/FEXInterpreter")
    if a.mode in ('gl32', 'gl64'):
        command = ['/usr/bin/FEXInterpreter', str(base / ('glx-probe' + a.mode[2:]))]
        env['ZINK_DEBUG'] = 'validation' if a.validation else ''
    if a.mode == 'runtime64':
        print('Guest memory:', '\n'.join(line for line in Path('/proc/meminfo').read_text().splitlines()
              if line.startswith(('MemTotal:', 'MemAvailable:', 'CommitLimit:', 'Committed_AS:'))), flush=True)
        print('Guest overcommit:', Path('/proc/sys/vm/overcommit_memory').read_text().strip(), flush=True)
        env.update(LD_LIBRARY_PATH='/usr/lib:/usr/lib32',
                   LIBGL_DRIVERS_PATH='/usr/lib/dri:/usr/lib32/dri',
                   GBM_BACKENDS_PATH='/usr/lib/gbm:/usr/lib32/gbm',
                   VK_DRIVER_FILES='/usr/share/vulkan/icd.d/asahi_icd.x86_64.json:/usr/share/vulkan/icd.d/asahi_icd.i686.json',
                   __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json')
        entry = '/home/e/.local/share/Steam/steamrt64/pv-runtime/steam-runtime-steamrt/_v2-entry-point'
        command = ['/usr/bin/FEXInterpreter', '/usr/bin/bash', entry, '--verbose', '--',
                   '/bin/bash', '/home/e/.cache/m4-qualification/steam-container-probe.sh']
    if a.gdb:
        command = ["/usr/bin/gdb", "-q", "-batch", "-ex", "set breakpoint pending on",
                   "-ex", "break _vk_queue_set_lost", "-ex", "run", "-ex", "bt",
                   "-ex", "info args", "-ex", "frame 1", "-ex", "info locals",
                   "--args", *command]
    print("Guest command:", command, "ICD:", env["VK_DRIVER_FILES"], flush=True)
    raise SystemExit(subprocess.run(command, env=env, timeout=a.seconds).returncode)

if not os.environ.get("M4_TEST_CPU_MAX_KHZ"):
    p.error("missing guard environment; invoke through gpu-experiment.py")
user = pwd.getpwnam("e")
runtime = Path(tempfile.mkdtemp(prefix="m4-muvm-"))
os.chown(runtime, user.pw_uid, user.pw_gid)
env = dict(os.environ, HOME=user.pw_dir, USER=user.pw_name, LOGNAME=user.pw_name,
           XDG_RUNTIME_DIR=str(runtime), ASAHI_M4_EXPERIMENTAL="1",
           VK_DRIVER_FILES="/opt/asahi-m4-port/asahi_icd.json",
           LD_LIBRARY_PATH="/opt/asahi-m4-port/lib",
           LIBGL_DRIVERS_PATH="/opt/asahi-m4-port/lib/dri",
           GBM_BACKENDS_PATH="/opt/asahi-m4-port/lib/gbm",
           __EGL_VENDOR_LIBRARY_FILENAMES="/opt/asahi-m4-port/egl.json",
           MESA_LOADER_DRIVER_OVERRIDE="zink", GALLIUM_DRIVER="zink",
           MESA_SHADER_CACHE_DISABLE="true", ASAHI_MESA_DEBUG="nocompress")
if a.mode not in ('gl32', 'gl64', 'runtime64'):
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY"):
        env.pop(key, None)
if a.host_drm_trace:
    env['LD_PRELOAD'] = str(base / 'drm-ioctl-errors.so')
command = ["runuser", "-u", "e", "--", "muvm", "--mem=8192" if a.mode == 'runtime64' else "--mem=2048", "--vram=512",
           "--cpu-list=0-3", "-i", "--", "/usr/bin/python3",
           str(base / "muvm-dx11-probe.py"), "--guest", "--mode", a.mode,
           "--seconds", str(a.seconds)]
if a.overlay:
    images = [Path('/usr/share/fex-emu/RootFS/default.erofs'),
              Path('/usr/share/fex-emu/overlays/mesa-i386.erofs'),
              Path('/usr/share/fex-emu/overlays/mesa-x86_64.erofs'), a.overlay]
    extra = []
    for image in images:
        assert image.is_file(), image
        extra += ['-f', str(image)]
    command[5:5] = extra
    command += ['--overlay', str(a.overlay)]
if a.gdb:
    command.append("--gdb")
if a.trace:
    command.append("--trace")
if a.validation:
    command.append("--validation")
proc = None
master, slave = pty.openpty()
def interrupted(signum, frame):
    raise SystemExit(128 + signum)
signal.signal(signal.SIGTERM, interrupted)
signal.signal(signal.SIGINT, interrupted)
try:
    print("Host command:", command, "runtime:", runtime, flush=True)
    # libkrun connects the first guest command to its console. Like the Steam
    # launcher, provide a PTY even when the outer guard logs through a pipe.
    proc = subprocess.Popen(command, env=env, cwd=user.pw_dir, start_new_session=True,
                            stdin=slave, stdout=slave, stderr=slave)
    os.close(slave)
    slave = -1
    deadline = time.monotonic() + a.seconds
    while proc.poll() is None:
        if time.monotonic() >= deadline:
            raise subprocess.TimeoutExpired(command, a.seconds)
        if select.select([master], [], [], .1)[0]:
            try:
                data = os.read(master, 65536)
            except OSError:
                break
            if data:
                os.write(1, data)
    raise SystemExit(proc.wait(timeout=3))
finally:
    if proc is not None:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=3)
        except ProcessLookupError:
            pass
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
    shutil.rmtree(runtime)
    os.close(master)
    if slave >= 0:
        os.close(slave)
